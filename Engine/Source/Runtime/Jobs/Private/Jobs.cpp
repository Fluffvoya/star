#include "Jobs/Jobs.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace star {

namespace {

class Scheduler;

constexpr std::size_t kPriorityCount = 3;

[[nodiscard]] constexpr std::size_t PriorityIndex(JobPriority priority) noexcept {
    return static_cast<std::size_t>(priority);
}

thread_local Scheduler *g_currentScheduler = nullptr;

} // namespace

namespace detail {

struct JobState final {
    explicit JobState(std::weak_ptr<Scheduler> owningScheduler) noexcept : scheduler(std::move(owningScheduler)) {}

    void SetRemaining(std::size_t count) noexcept { remaining.store(count, std::memory_order_relaxed); }

    void CompleteOne() noexcept {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) != 1) {
            return;
        }

        {
            const std::lock_guard lock(mutex);
            complete.store(true, std::memory_order_release);
        }
        completed.notify_all();
    }

    [[nodiscard]] bool IsComplete() const noexcept { return complete.load(std::memory_order_acquire); }

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
        const std::uint32_t hardwareThreads = std::thread::hardware_concurrency();
        const std::uint32_t workerCount = hardwareThreads > 1 ? hardwareThreads - 1 : 1;

        m_queues.reserve(workerCount);
        for (std::uint32_t index = 0; index < workerCount; ++index) {
            m_queues.push_back(std::make_unique<WorkerQueue>());
        }

        try {
            m_workers.reserve(workerCount);
            for (std::uint32_t index = 0; index < workerCount; ++index) {
                m_workers.emplace_back(&Scheduler::WorkerLoop, this, index);
            }
        } catch (...) {
            RequestStop();
            m_workers.clear();
        }
    }

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

    ~Scheduler() {
        {
            const std::lock_guard submissionLock(m_submissionMutex);
            m_accepting = false;
        }

        if (m_workers.empty()) {
            while (TryExecuteOne()) {
            }
        }

        std::unique_lock idleLock(m_idleMutex);
        m_idle.wait(idleLock, [this] { return m_pending.load(std::memory_order_acquire) == 0; });
        idleLock.unlock();

        RequestStop();
        m_workers.clear();
    }

    [[nodiscard]] bool Submit(const std::shared_ptr<detail::JobState> &state, JobPriority priority,
                              std::move_only_function<void()> function) {
        {
            const std::lock_guard submissionLock(m_submissionMutex);
            if (!m_accepting) {
                return false;
            }

            m_pending.fetch_add(1, std::memory_order_relaxed);

            std::move_only_function<void()> runnable = [this, state, function = std::move(function)]() mutable {
                try {
                    function();
                } catch (...) {
                    std::terminate();
                }

                TaskFinished();
                state->CompleteOne();
            };

            Push(Task{priority, std::move(runnable)});
        }

        return true;
    }

    [[nodiscard]] bool TryExecuteOne() {
        const std::size_t queueCount = m_queues.size();
        if (queueCount == 0) {
            return false;
        }

        const std::size_t start = m_executionCursor.fetch_add(1, std::memory_order_relaxed) % queueCount;
        std::optional<Task> task = TryTake(start);
        if (!task) {
            return false;
        }

        task->function();
        return true;
    }

    [[nodiscard]] std::uint32_t WorkerCount() const noexcept { return static_cast<std::uint32_t>(m_workers.size()); }

    [[nodiscard]] bool IsWorkerThread() const noexcept { return g_currentScheduler == this; }

  private:
    struct Task final {
        JobPriority priority = JobPriority::Normal;
        std::move_only_function<void()> function;
    };

    struct WorkerQueue final {
        mutable std::mutex mutex;
        std::array<std::deque<Task>, kPriorityCount> tasks;
    };

    void Push(Task task) {
        const std::size_t queueCount = m_queues.size();
        if (queueCount == 0) {
            return;
        }

        const std::size_t owner = m_queueCursor.fetch_add(1, std::memory_order_relaxed) % queueCount;
        WorkerQueue &queue = *m_queues[owner];

        {
            const std::lock_guard wakeLock(m_wakeMutex);
            const std::lock_guard queueLock(queue.mutex);
            queue.tasks[PriorityIndex(task.priority)].push_back(std::move(task));
        }
        m_available.notify_one();
    }

    [[nodiscard]] std::optional<Task> TryTake(std::size_t preferredQueue) {
        const std::size_t queueCount = m_queues.size();
        if (queueCount == 0) {
            return std::nullopt;
        }

        WorkerQueue &localQueue = *m_queues[preferredQueue];
        for (std::size_t priority = 0; priority < kPriorityCount; ++priority) {
            const std::lock_guard lock(localQueue.mutex);
            std::deque<Task> &tasks = localQueue.tasks[priority];
            if (!tasks.empty()) {
                Task task = std::move(tasks.back());
                tasks.pop_back();
                return task;
            }
        }

        for (std::size_t offset = 1; offset < queueCount; ++offset) {
            const std::size_t victimIndex = (preferredQueue + offset) % queueCount;
            WorkerQueue &victim = *m_queues[victimIndex];

            for (std::size_t priority = 0; priority < kPriorityCount; ++priority) {
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

    [[nodiscard]] bool HasWork() const {
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

    void TaskFinished() noexcept {
        if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            m_idle.notify_all();
        }
    }

    void RequestStop() noexcept {
        {
            const std::lock_guard wakeLock(m_wakeMutex);
            m_stopping.store(true, std::memory_order_release);
        }
        m_available.notify_all();
    }

    void WorkerLoop(std::size_t workerIndex) {
        g_currentScheduler = this;

        while (true) {
            if (std::optional<Task> task = TryTake(workerIndex)) {
                task->function();
                continue;
            }

            std::unique_lock wakeLock(m_wakeMutex);
            m_available.wait(wakeLock, [this] { return m_stopping.load(std::memory_order_acquire) || HasWork(); });

            if (m_stopping.load(std::memory_order_acquire) && !HasWork()) {
                break;
            }
        }

        g_currentScheduler = nullptr;
    }

    std::vector<std::unique_ptr<WorkerQueue>> m_queues;
    std::vector<std::jthread> m_workers;
    std::mutex m_submissionMutex;
    std::mutex m_idleMutex;
    mutable std::mutex m_wakeMutex;
    std::condition_variable m_available;
    std::condition_variable m_idle;
    std::atomic<std::size_t> m_pending{0};
    std::atomic<std::size_t> m_queueCursor{0};
    std::atomic<std::size_t> m_executionCursor{0};
    std::atomic<bool> m_stopping{false};
    bool m_accepting = true;
};

[[nodiscard]] std::shared_ptr<Scheduler> GetScheduler() {
    static const std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
    return scheduler;
}

} // namespace

JobHandle::JobHandle(std::shared_ptr<detail::JobState> state) noexcept : m_state(std::move(state)) {}

void JobHandle::Wait() {
    if (!m_state || m_state->IsComplete()) {
        return;
    }

    const std::shared_ptr<Scheduler> scheduler = m_state->scheduler.lock();
    while (!m_state->IsComplete()) {
        if (scheduler && scheduler->TryExecuteOne()) {
            continue;
        }

        std::unique_lock lock(m_state->mutex);
        m_state->completed.wait(lock, [this] { return m_state->IsComplete(); });
    }
}

bool JobHandle::IsComplete() const noexcept { return !m_state || m_state->IsComplete(); }

bool JobHandle::IsValid() const noexcept { return static_cast<bool>(m_state); }

JobHandle Jobs::Run(std::move_only_function<void()> job, JobPriority priority) {
    const std::shared_ptr<Scheduler> scheduler = GetScheduler();
    auto state = std::make_shared<detail::JobState>(scheduler);

    if (!scheduler->Submit(state, priority, std::move(job))) {
        state->CompleteOne();
    }

    return JobHandle(std::move(state));
}

JobHandle Jobs::ParallelForIndices(std::size_t count, std::size_t grainSize,
                                   std::move_only_function<void(std::size_t)> function) {
    if (count == 0) {
        auto state = std::make_shared<detail::JobState>(std::weak_ptr<Scheduler>{});
        state->CompleteOne();
        return JobHandle(std::move(state));
    }

    grainSize = std::max<std::size_t>(grainSize, 1);
    const std::size_t taskCount = count / grainSize + (count % grainSize == 0 ? 0 : 1);

    const std::shared_ptr<Scheduler> scheduler = GetScheduler();
    auto state = std::make_shared<detail::JobState>(scheduler);
    state->SetRemaining(taskCount);

    auto sharedFunction = std::make_shared<std::move_only_function<void(std::size_t)>>(std::move(function));

    for (std::size_t begin = 0; begin < count; begin += grainSize) {
        const std::size_t end = std::min(begin + grainSize, count);
        std::move_only_function<void()> chunk = [state, sharedFunction, begin, end] {
            for (std::size_t index = begin; index < end; ++index) {
                (*sharedFunction)(index);
            }
        };

        if (!scheduler->Submit(state, JobPriority::Normal, std::move(chunk))) {
            state->CompleteOne();
        }
    }

    return JobHandle(std::move(state));
}

std::uint32_t Jobs::GetWorkerCount() { return GetScheduler()->WorkerCount(); }

bool Jobs::IsWorkerThread() noexcept { return g_currentScheduler != nullptr; }

} // namespace star
