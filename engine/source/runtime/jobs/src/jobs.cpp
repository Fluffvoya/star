#include "jobs/jobs.hpp"
#include "jobs/task_graph.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace star {

namespace {

class Scheduler;

constexpr std::size_t k_priority_count = 3;

[[nodiscard]] constexpr std::size_t priorityIndex(JobPriority priority) noexcept {
    return static_cast<std::size_t>(priority);
}

thread_local Scheduler *g_current_scheduler = nullptr;

[[nodiscard]] std::uint32_t defaultWorkerCount() noexcept {
    const std::uint32_t hardware_threads = std::thread::hardware_concurrency();
    return hardware_threads > 1 ? hardware_threads - 1 : 1;
}

[[nodiscard]] std::uint32_t configuredWorkerCount() noexcept {
    const char *value = std::getenv("STAR_JOBS_WORKERS");
    if (value == nullptr || *value == '\0') {
        return defaultWorkerCount();
    }

    const std::string_view text(value);
    std::uint32_t worker_count = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), worker_count);
    if (error != std::errc{} || end != text.data() + text.size() || worker_count == 0) {
        return defaultWorkerCount();
    }
    return worker_count;
}

} // namespace

namespace detail {

struct JobState final {
    explicit JobState(std::weak_ptr<Scheduler> owning_scheduler) noexcept : scheduler(std::move(owning_scheduler)) {}

    void setRemaining(std::size_t count) noexcept { remaining.store(count, std::memory_order_relaxed); }

    void completeOne() noexcept {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) != 1) {
            return;
        }

        {
            const std::lock_guard lock(mutex);
            complete.store(true, std::memory_order_release);
        }
        completed.notify_all();
    }

    [[nodiscard]] bool isComplete() const noexcept { return complete.load(std::memory_order_acquire); }

    std::weak_ptr<Scheduler> scheduler;
    std::atomic<std::size_t> remaining{1};
    std::atomic<bool> complete{false};
    mutable std::mutex mutex;
    std::condition_variable completed;
};

} // namespace detail

namespace {

class Scheduler final {
  public:
    Scheduler() {
        const std::uint32_t worker_count = configuredWorkerCount();

        m_queues.reserve(worker_count);
        for (std::uint32_t index = 0; index < worker_count; ++index) {
            m_queues.push_back(std::make_unique<WorkerQueue>());
        }

        try {
            m_workers.reserve(worker_count);
            for (std::uint32_t index = 0; index < worker_count; ++index) {
                m_workers.emplace_back(&Scheduler::workerLoop, this, index);
            }
        } catch (...) {
            requestStop();
            m_workers.clear();
        }
    }

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

    ~Scheduler() {
        {
            const std::lock_guard submission_lock(m_submission_mutex);
            m_accepting = false;
        }

        if (m_workers.empty()) {
            while (tryExecuteOne()) {
            }
        }

        std::unique_lock idle_lock(m_idle_mutex);
        m_idle.wait(idle_lock, [this] { return m_pending.load(std::memory_order_acquire) == 0; });
        idle_lock.unlock();

        requestStop();
        m_workers.clear();
    }

    [[nodiscard]] bool submit(const std::shared_ptr<detail::JobState> &state, JobPriority priority,
                              std::move_only_function<void()> function) {
        {
            const std::lock_guard submission_lock(m_submission_mutex);
            if (!m_accepting) {
                return false;
            }

            std::move_only_function<void()> runnable = [this, state, function = std::move(function)]() mutable {
                try {
                    function();
                } catch (...) {
                    std::terminate();
                }

                taskFinished();
                state->completeOne();
            };

            m_pending.fetch_add(1, std::memory_order_relaxed);
            try {
                push(Task{priority, std::move(runnable)});
            } catch (...) {
                taskFinished();
                throw;
            }
        }

        return true;
    }

    [[nodiscard]] bool tryExecuteOne() {
        const std::size_t queue_count = m_queues.size();
        if (queue_count == 0) {
            return false;
        }

        const std::size_t start = m_execution_cursor.fetch_add(1, std::memory_order_relaxed) % queue_count;
        std::optional<Task> task = tryTake(start);
        if (!task) {
            return false;
        }

        task->function();
        return true;
    }

    [[nodiscard]] std::uint32_t workerCount() const noexcept { return static_cast<std::uint32_t>(m_workers.size()); }

    [[nodiscard]] bool isWorkerThread() const noexcept { return g_current_scheduler == this; }

  private:
    struct Task final {
        JobPriority priority = JobPriority::Normal;
        std::move_only_function<void()> function;
    };

    struct WorkerQueue final {
        mutable std::mutex mutex;
        std::array<std::deque<Task>, k_priority_count> tasks;
    };

    void push(Task task) {
        const std::size_t queue_count = m_queues.size();
        if (queue_count == 0) {
            return;
        }

        const std::size_t owner = m_queue_cursor.fetch_add(1, std::memory_order_relaxed) % queue_count;
        WorkerQueue &queue = *m_queues[owner];

        {
            const std::lock_guard wake_lock(m_wake_mutex);
            const std::lock_guard queue_lock(queue.mutex);
            queue.tasks[priorityIndex(task.priority)].push_back(std::move(task));
        }
        m_available.notify_one();
    }

    [[nodiscard]] std::optional<Task> tryTake(std::size_t preferred_queue) {
        const std::size_t queue_count = m_queues.size();
        if (queue_count == 0) {
            return std::nullopt;
        }

        WorkerQueue &local_queue = *m_queues[preferred_queue];
        for (std::size_t priority = 0; priority < k_priority_count; ++priority) {
            const std::lock_guard lock(local_queue.mutex);
            std::deque<Task> &tasks = local_queue.tasks[priority];
            if (!tasks.empty()) {
                Task task = std::move(tasks.back());
                tasks.pop_back();
                return task;
            }
        }

        for (std::size_t offset = 1; offset < queue_count; ++offset) {
            const std::size_t victim_index = (preferred_queue + offset) % queue_count;
            WorkerQueue &victim = *m_queues[victim_index];

            for (std::size_t priority = 0; priority < k_priority_count; ++priority) {
                const std::lock_guard lock(victim.mutex);
                std::deque<Task> &tasks = victim.tasks[priority];
                if (!tasks.empty()) {
                    Task task = std::move(tasks.front());
                    tasks.pop_front();
                    return task;
                }
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] bool hasWork() const {
        for (const std::unique_ptr<WorkerQueue> &queue : m_queues) {
            const std::lock_guard lock(queue->mutex);
            for (const std::deque<Task> &tasks : queue->tasks) {
                if (!tasks.empty()) {
                    return true;
                }
            }
        }
        return false;
    }

    void taskFinished() noexcept {
        if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            const std::lock_guard lock(m_idle_mutex);
            m_idle.notify_all();
        }
    }

    void requestStop() noexcept {
        {
            const std::lock_guard wake_lock(m_wake_mutex);
            m_stopping.store(true, std::memory_order_release);
        }
        m_available.notify_all();
    }

    void workerLoop(std::size_t worker_index) {
        g_current_scheduler = this;

        while (true) {
            if (std::optional<Task> task = tryTake(worker_index)) {
                task->function();
                continue;
            }

            std::unique_lock wake_lock(m_wake_mutex);
            m_available.wait(wake_lock, [this] { return m_stopping.load(std::memory_order_acquire) || hasWork(); });

            if (m_stopping.load(std::memory_order_acquire) && !hasWork()) {
                break;
            }
        }

        g_current_scheduler = nullptr;
    }

    std::vector<std::unique_ptr<WorkerQueue>> m_queues;
    std::vector<std::jthread> m_workers;
    std::mutex m_submission_mutex;
    std::mutex m_idle_mutex;
    mutable std::mutex m_wake_mutex;
    std::condition_variable m_available;
    std::condition_variable m_idle;
    std::atomic<std::size_t> m_pending{0};
    std::atomic<std::size_t> m_queue_cursor{0};
    std::atomic<std::size_t> m_execution_cursor{0};
    std::atomic<bool> m_stopping{false};
    bool m_accepting = true;
};

[[nodiscard]] std::shared_ptr<Scheduler> getScheduler() {
    static const std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
    return scheduler;
}

} // namespace

JobHandle::JobHandle(std::shared_ptr<detail::JobState> state) noexcept : m_state(std::move(state)) {}

void JobHandle::wait() {
    if (!m_state || m_state->isComplete()) {
        return;
    }

    const std::shared_ptr<Scheduler> scheduler = m_state->scheduler.lock();
    while (!m_state->isComplete()) {
        if (scheduler && scheduler->tryExecuteOne()) {
            continue;
        }

        std::unique_lock lock(m_state->mutex);
        m_state->completed.wait(lock, [this] { return m_state->isComplete(); });
    }
}

bool JobHandle::isComplete() const noexcept { return !m_state || m_state->isComplete(); }

bool JobHandle::isValid() const noexcept { return static_cast<bool>(m_state); }

JobHandle Jobs::run(std::move_only_function<void()> job, JobPriority priority) {
    const std::shared_ptr<Scheduler> scheduler = getScheduler();
    auto state = std::make_shared<detail::JobState>(scheduler);

    if (!scheduler->submit(state, priority, std::move(job))) {
        state->completeOne();
    }

    return JobHandle(std::move(state));
}

JobHandle Jobs::parallelForIndices(std::size_t count, std::size_t grain_size,
                                   std::move_only_function<void(std::size_t)> function) {
    if (count == 0) {
        auto state = std::make_shared<detail::JobState>(std::weak_ptr<Scheduler>{});
        state->completeOne();
        return JobHandle(std::move(state));
    }

    grain_size = std::max<std::size_t>(grain_size, 1);
    const std::size_t task_count = count / grain_size + (count % grain_size == 0 ? 0 : 1);

    const std::shared_ptr<Scheduler> scheduler = getScheduler();
    auto state = std::make_shared<detail::JobState>(scheduler);
    state->setRemaining(task_count);

    auto shared_function = std::make_shared<std::move_only_function<void(std::size_t)>>(std::move(function));

    for (std::size_t begin = 0; begin < count; begin += grain_size) {
        const std::size_t end = std::min(begin + grain_size, count);
        std::move_only_function<void()> chunk = [state, shared_function, begin, end] {
            for (std::size_t index = begin; index < end; ++index) {
                (*shared_function)(index);
            }
        };

        if (!scheduler->submit(state, JobPriority::Normal, std::move(chunk))) {
            state->completeOne();
        }
    }

    return JobHandle(std::move(state));
}

std::uint32_t Jobs::getWorkerCount() { return getScheduler()->workerCount(); }

bool Jobs::isWorkerThread() noexcept { return g_current_scheduler != nullptr; }

struct TaskGraph::Execution {
    static constexpr std::size_t no_task = std::numeric_limits<std::size_t>::max();

    struct DependencyState {
        std::atomic<std::size_t> remaining{0};
        std::atomic<bool> blocked{false};
        std::size_t next_unsubmitted = no_task;
    };

    Execution(std::vector<Node> &nodes, const std::shared_ptr<Scheduler> &scheduler)
        : m_nodes(nodes), m_scheduler(*scheduler), m_completion(std::make_shared<detail::JobState>(scheduler)),
          m_dependencies(std::make_unique<DependencyState[]>(nodes.size())) {
        m_completion->setRemaining(nodes.size());
        for (std::size_t index = 0; index < m_nodes.size(); ++index) {
            m_dependencies[index].remaining.store(m_nodes[index].prerequisites, std::memory_order_relaxed);
        }
    }

    void start() noexcept {
        std::size_t unsubmitted = no_task;
        for (std::size_t index = 0; index < m_nodes.size(); ++index) {
            if (m_nodes[index].prerequisites == 0) {
                enqueue(index, unsubmitted);
            }
        }
        drainUnsubmitted(unsubmitted);
    }

    [[nodiscard]] const std::shared_ptr<detail::JobState> &getCompletion() const noexcept { return m_completion; }

    [[nodiscard]] std::expected<void, TaskGraphError> getResult() const noexcept {
        if (m_submission_failed.load(std::memory_order_relaxed)) {
            return std::unexpected(TaskGraphError::SubmissionFailed);
        }
        if (m_task_failed.load(std::memory_order_relaxed)) {
            return std::unexpected(TaskGraphError::TaskFailed);
        }
        return {};
    }

  private:
    void enqueue(std::size_t index, std::size_t &unsubmitted) noexcept {
        try {
            if (m_scheduler.submit(m_completion, m_nodes[index].priority, [this, index] { execute(index); })) {
                return;
            }
        } catch (...) {
            // A ready node can still be retired without allocating storage for another queue entry.
        }
        m_submission_failed.store(true, std::memory_order_relaxed);
        m_dependencies[index].next_unsubmitted = unsubmitted;
        unsubmitted = index;
    }

    void releaseDependents(std::size_t index, bool succeeded, std::size_t &unsubmitted) noexcept {
        for (const std::size_t dependent : m_nodes[index].dependents) {
            auto &state = m_dependencies[dependent];
            if (!succeeded) {
                state.blocked.store(true, std::memory_order_relaxed);
            }
            // The final prerequisite observes the writes released by all preceding prerequisites.
            if (state.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                enqueue(dependent, unsubmitted);
            }
        }
    }

    void drainUnsubmitted(std::size_t unsubmitted) noexcept {
        while (unsubmitted != no_task) {
            const auto index = unsubmitted;
            unsubmitted = m_dependencies[index].next_unsubmitted;
            releaseDependents(index, false, unsubmitted);
            m_completion->completeOne();
        }
    }

    void execute(std::size_t index) noexcept {
        bool succeeded = false;
        if (!m_dependencies[index].blocked.load(std::memory_order_relaxed)) {
            try {
                succeeded = m_nodes[index].task();
            } catch (...) {
                // The graph reports a failed callback and skips its dependents before returning to the caller.
            }
            if (!succeeded) {
                m_task_failed.store(true, std::memory_order_relaxed);
            }
        }
        std::size_t unsubmitted = no_task;
        releaseDependents(index, succeeded, unsubmitted);
        drainUnsubmitted(unsubmitted);
    }

    std::vector<Node> &m_nodes;
    Scheduler &m_scheduler;
    std::shared_ptr<detail::JobState> m_completion;
    std::unique_ptr<DependencyState[]> m_dependencies;
    std::atomic<bool> m_submission_failed{false};
    std::atomic<bool> m_task_failed{false};
};

std::expected<TaskGraph::TaskId, TaskGraphError> TaskGraph::addTask(Task task, JobPriority priority) noexcept {
    if (!task) {
        return std::unexpected(TaskGraphError::InvalidTask);
    }
    if (priorityIndex(priority) >= k_priority_count) {
        return std::unexpected(TaskGraphError::InvalidPriority);
    }
    const std::lock_guard lock(m_mutex);
    if (m_running) {
        return std::unexpected(TaskGraphError::Busy);
    }
    try {
        const auto index = m_nodes.size();
        m_nodes.push_back(Node{std::move(task), priority, {}});
        return TaskId(*this, index);
    } catch (...) {
        return std::unexpected(TaskGraphError::AllocationFailed);
    }
}

std::expected<void, TaskGraphError> TaskGraph::addDependency(TaskId prerequisite, TaskId dependent) noexcept {
    const std::lock_guard lock(m_mutex);
    if (m_running) {
        return std::unexpected(TaskGraphError::Busy);
    }
    if (prerequisite.m_owner != this || dependent.m_owner != this || prerequisite.m_index >= m_nodes.size() ||
        dependent.m_index >= m_nodes.size()) {
        return std::unexpected(TaskGraphError::InvalidTask);
    }
    if (prerequisite == dependent) {
        return std::unexpected(TaskGraphError::Cycle);
    }
    auto &dependents = m_nodes[prerequisite.m_index].dependents;
    if (std::ranges::find(dependents, dependent.m_index) != dependents.end()) {
        return {};
    }
    try {
        dependents.push_back(dependent.m_index);
        ++m_nodes[dependent.m_index].prerequisites;
        return {};
    } catch (...) {
        return std::unexpected(TaskGraphError::AllocationFailed);
    }
}

bool TaskGraph::isAcyclic() const {
    std::vector<std::size_t> remaining;
    std::vector<std::size_t> ready;
    remaining.reserve(m_nodes.size());
    ready.reserve(m_nodes.size());
    for (std::size_t index = 0; index < m_nodes.size(); ++index) {
        remaining.push_back(m_nodes[index].prerequisites);
        if (remaining.back() == 0) {
            ready.push_back(index);
        }
    }
    for (std::size_t cursor = 0; cursor < ready.size(); ++cursor) {
        for (const auto dependent : m_nodes[ready[cursor]].dependents) {
            if (--remaining[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }
    return ready.size() == m_nodes.size();
}

std::expected<void, TaskGraphError> TaskGraph::run() noexcept {
    {
        const std::lock_guard lock(m_mutex);
        if (m_running) {
            return std::unexpected(TaskGraphError::Busy);
        }
        if (m_nodes.empty()) {
            return {};
        }
        m_running = true;
    }
    struct RunGuard {
        std::mutex &mutex;
        bool &running;
        ~RunGuard() {
            const std::lock_guard lock(mutex);
            running = false;
        }
    } guard{m_mutex, m_running};

    std::shared_ptr<Scheduler> scheduler;
    std::optional<Execution> execution;
    try {
        if (!isAcyclic()) {
            return std::unexpected(TaskGraphError::Cycle);
        }
        scheduler = getScheduler();
        execution.emplace(m_nodes, scheduler);
    } catch (...) {
        return std::unexpected(TaskGraphError::AllocationFailed);
    }
    execution->start();
    JobHandle(execution->getCompletion()).wait();
    return execution->getResult();
}

} // namespace star
