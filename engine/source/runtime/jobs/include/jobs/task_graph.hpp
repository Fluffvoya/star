#pragma once

#include "jobs/jobs.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <vector>

namespace star {

enum class TaskGraphError : std::uint8_t {
    InvalidTask,
    InvalidPriority,
    Busy,
    Cycle,
    TaskFailed,
    SubmissionFailed,
    AllocationFailed,
};

class STAR_JOBS_API TaskGraph final {
  public:
    using Task = std::move_only_function<bool()>;

    class TaskId final {
      public:
        TaskId() = default;
        friend bool operator==(TaskId, TaskId) = default;

      private:
        friend class TaskGraph;

        TaskId(const TaskGraph &owner, std::size_t index) noexcept : m_owner(&owner), m_index(index) {}

        const TaskGraph *m_owner = nullptr;
        std::size_t m_index = 0;
    };

    [[nodiscard]] std::expected<TaskId, TaskGraphError> addTask(Task task,
                                                                JobPriority priority = JobPriority::Normal) noexcept;
    [[nodiscard]] std::expected<void, TaskGraphError> addDependency(TaskId prerequisite, TaskId dependent) noexcept;

    // A task succeeds by returning true. Failure skips its dependents and drains independent work before returning.
    // The same graph can be rerun after completion. Keep the graph and its captures alive for every execution.
    [[nodiscard]] std::expected<void, TaskGraphError> run() noexcept;

  private:
    struct Node {
        Task task;
        JobPriority priority;
        std::vector<std::size_t> dependents;
        std::size_t prerequisites = 0;
    };

    struct Execution;

    [[nodiscard]] bool isAcyclic() const;

    std::vector<Node> m_nodes;
    std::mutex m_mutex;
    bool m_running = false;
};

} // namespace star
