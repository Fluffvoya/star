#include "test_support.hpp"

#include "jobs/jobs.hpp"

#include <atomic>
#include <cstddef>
#include <vector>

namespace {

STAR_TEST(runExecutesAndCompletes) {
    std::atomic<int> call_count = 0;

    star::JobHandle handle = star::Jobs::run([&call_count] { call_count.fetch_add(1, std::memory_order_relaxed); });
    STAR_CHECK(handle.isValid());
    handle.wait();

    STAR_CHECK(handle.isComplete());
    STAR_CHECK(call_count.load(std::memory_order_relaxed) == 1);
}

STAR_TEST(parallelForIndexRangeVisitsEveryIndexOnce) {
    constexpr std::size_t k_item_count = 1024;
    std::vector<int> visits(k_item_count);

    star::JobHandle handle =
        star::Jobs::parallelFor(k_item_count, 37, [&visits](std::size_t index) { visits[index] += 1; });
    handle.wait();

    int total = 0;
    for (const int visit_count : visits) {
        STAR_CHECK(visit_count == 1);
        total += visit_count;
    }
    STAR_CHECK(total == static_cast<int>(k_item_count));
}

STAR_TEST(parallelForRangeVisitsEveryElementOnce) {
    std::vector<int> values(257);
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = static_cast<int>(index + 1);
    }

    std::atomic<std::int64_t> total = 0;
    star::JobHandle handle =
        star::Jobs::parallelFor(values, 13, [&total](const int &value) { total.fetch_add(value); });
    handle.wait();

    STAR_CHECK(total.load() == 257 * 258 / 2);
}

STAR_TEST(parallelForEmptyRangeCompletes) {
    std::atomic<int> call_count = 0;

    star::JobHandle handle = star::Jobs::parallelFor(
        0, 16, [&call_count](std::size_t) { call_count.fetch_add(1, std::memory_order_relaxed); });
    handle.wait();

    STAR_CHECK(handle.isComplete());
    STAR_CHECK(call_count.load(std::memory_order_relaxed) == 0);
}

STAR_TEST(zeroGrainIsTreatedAsOne) {
    std::atomic<int> call_count = 0;

    star::JobHandle handle = star::Jobs::parallelFor(
        4, 0, [&call_count](std::size_t) { call_count.fetch_add(1, std::memory_order_relaxed); });
    handle.wait();

    STAR_CHECK(call_count.load(std::memory_order_relaxed) == 4);
}

STAR_TEST(nestedWaitAvoidsWorkerStarvation) {
    std::atomic<int> stage_count = 0;

    star::JobHandle outer = star::Jobs::run([&stage_count] {
        stage_count.fetch_add(1, std::memory_order_relaxed);
        star::JobHandle inner =
            star::Jobs::run([&stage_count] { stage_count.fetch_add(1, std::memory_order_relaxed); });
        inner.wait();
    });
    outer.wait();

    STAR_CHECK(stage_count.load(std::memory_order_relaxed) == 2);
}

STAR_TEST(backgroundPriorityRunsAndWaits) {
    std::atomic<bool> ran = false;

    star::JobHandle handle =
        star::Jobs::run([&ran] { ran.store(true, std::memory_order_release); }, star::JobPriority::Background);
    handle.wait();

    STAR_CHECK(ran.load(std::memory_order_acquire));
}

} // namespace
