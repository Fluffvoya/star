#include "TestSupport.hpp"

#include "Jobs/Jobs.hpp"

#include <atomic>
#include <cstddef>
#include <vector>

namespace {

STAR_TEST(RunExecutesAndCompletes) {
    std::atomic<int> callCount = 0;

    star::JobHandle handle = star::Jobs::Run([&callCount] { callCount.fetch_add(1, std::memory_order_relaxed); });
    STAR_CHECK(handle.IsValid());
    handle.Wait();

    STAR_CHECK(handle.IsComplete());
    STAR_CHECK(callCount.load(std::memory_order_relaxed) == 1);
}

STAR_TEST(ParallelForIndexRangeVisitsEveryIndexOnce) {
    constexpr std::size_t kItemCount = 1024;
    std::vector<int> visits(kItemCount);

    star::JobHandle handle =
        star::Jobs::ParallelFor(kItemCount, 37, [&visits](std::size_t index) { visits[index] += 1; });
    handle.Wait();

    int total = 0;
    for (const int visitCount : visits) {
        STAR_CHECK(visitCount == 1);
        total += visitCount;
    }
    STAR_CHECK(total == static_cast<int>(kItemCount));
}

STAR_TEST(ParallelForRangeVisitsEveryElementOnce) {
    std::vector<int> values(257);
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = static_cast<int>(index + 1);
    }

    std::atomic<std::int64_t> total = 0;
    star::JobHandle handle =
        star::Jobs::ParallelFor(values, 13, [&total](const int &value) { total.fetch_add(value); });
    handle.Wait();

    STAR_CHECK(total.load() == 257 * 258 / 2);
}

STAR_TEST(ParallelForEmptyRangeCompletes) {
    std::atomic<int> callCount = 0;

    star::JobHandle handle = star::Jobs::ParallelFor(
        0, 16, [&callCount](std::size_t) { callCount.fetch_add(1, std::memory_order_relaxed); });
    handle.Wait();

    STAR_CHECK(handle.IsComplete());
    STAR_CHECK(callCount.load(std::memory_order_relaxed) == 0);
}

STAR_TEST(ZeroGrainIsTreatedAsOne) {
    std::atomic<int> callCount = 0;

    star::JobHandle handle =
        star::Jobs::ParallelFor(4, 0, [&callCount](std::size_t) { callCount.fetch_add(1, std::memory_order_relaxed); });
    handle.Wait();

    STAR_CHECK(callCount.load(std::memory_order_relaxed) == 4);
}

STAR_TEST(NestedWaitAvoidsWorkerStarvation) {
    std::atomic<int> stageCount = 0;

    star::JobHandle outer = star::Jobs::Run([&stageCount] {
        stageCount.fetch_add(1, std::memory_order_relaxed);
        star::JobHandle inner = star::Jobs::Run([&stageCount] { stageCount.fetch_add(1, std::memory_order_relaxed); });
        inner.Wait();
    });
    outer.Wait();

    STAR_CHECK(stageCount.load(std::memory_order_relaxed) == 2);
}

STAR_TEST(BackgroundPriorityRunsAndWaits) {
    std::atomic<bool> ran = false;

    star::JobHandle handle =
        star::Jobs::Run([&ran] { ran.store(true, std::memory_order_release); }, star::JobPriority::Background);
    handle.Wait();

    STAR_CHECK(ran.load(std::memory_order_acquire));
}

} // namespace
