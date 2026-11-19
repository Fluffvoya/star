#pragma once

#include "jobs/export.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

namespace star {

namespace detail {

struct JobState;

} // namespace detail

enum class JobPriority : std::uint8_t {
    High,
    Normal,
    Background,
};

class STAR_JOBS_API JobHandle final {
  public:
    JobHandle() noexcept = default;
    JobHandle(const JobHandle &) noexcept = default;
    JobHandle(JobHandle &&) noexcept = default;
    JobHandle &operator=(const JobHandle &) noexcept = default;
    JobHandle &operator=(JobHandle &&) noexcept = default;
    ~JobHandle() = default;

    // Waits for completion. While waiting, the calling thread executes available jobs from the same system.
    void wait();

    [[nodiscard]] bool isComplete() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;

  private:
    friend class Jobs;
    friend class TaskGraph;

    explicit JobHandle(std::shared_ptr<detail::JobState> state) noexcept;

    std::shared_ptr<detail::JobState> m_state;
};

class STAR_JOBS_API Jobs final {
  public:
    // Runs one job asynchronously. A job function must not allow exceptions to escape.
    [[nodiscard]] static JobHandle run(std::move_only_function<void()> job, JobPriority priority = JobPriority::Normal);

    // Invokes function once for each index in [0, count). Work is split into chunks of at least grain_size items.
    template <typename Function>
        requires std::invocable<std::decay_t<Function> &, std::size_t>
    [[nodiscard]] static JobHandle parallelFor(std::size_t count, std::size_t grain_size, Function &&function) {
        return parallelForIndices(count, grain_size,
                                  std::move_only_function<void(std::size_t)>(std::forward<Function>(function)));
    }

    // Invokes function once for every element. The range must outlive the returned handle.
    template <std::ranges::random_access_range Range, typename Function>
        requires std::invocable<std::decay_t<Function> &, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static JobHandle parallelFor(Range &range, std::size_t grain_size, Function &&function) {
        using Difference = std::ranges::range_difference_t<Range>;

        auto first = std::ranges::begin(range);
        const auto count = static_cast<std::size_t>(std::ranges::size(range));
        return parallelFor(count, grain_size,
                           [first, function = std::forward<Function>(function)](std::size_t index) mutable {
                               std::invoke(function, *(first + static_cast<Difference>(index)));
                           });
    }

    [[nodiscard]] static std::uint32_t getWorkerCount();
    [[nodiscard]] static bool isWorkerThread() noexcept;

  private:
    [[nodiscard]] static JobHandle parallelForIndices(std::size_t count, std::size_t grain_size,
                                                      std::move_only_function<void(std::size_t)> function);
};

} // namespace star
