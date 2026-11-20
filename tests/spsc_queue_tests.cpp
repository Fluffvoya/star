#include "test_support.hpp"

#include "core/spsc_queue.hpp"

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

namespace {

struct TrackedValue {
    int *count;

    explicit TrackedValue(int &live_count) noexcept : count(&live_count) { ++*count; }
    TrackedValue(TrackedValue &&other) noexcept : count(std::exchange(other.count, nullptr)) {}
    ~TrackedValue() {
        if (count != nullptr) {
            --*count;
        }
    }
};

struct CheckedValue {
    int value;

    explicit CheckedValue(int input) : value(input) {
        if (input < 0) {
            throw std::invalid_argument("Negative input");
        }
    }
};

STAR_TEST(spscQueueWrapsAndReturnsValuesInOrder) {
    star::SPSCQueue<int> queue(2);
    STAR_CHECK(queue.capacity() == 2);
    STAR_CHECK(!queue.tryPop());
    STAR_CHECK(queue.tryPush(1));
    STAR_CHECK(queue.tryEmplace(2));
    STAR_CHECK(!queue.tryPush(3));
    STAR_CHECK(queue.tryPop() == 1);
    STAR_CHECK(queue.tryPush(3));
    STAR_CHECK(queue.tryPop() == 2);
    STAR_CHECK(queue.tryPop() == 3);
    STAR_CHECK(!queue.tryPop());

    star::SPSCQueue<int> disabled(0);
    STAR_CHECK(disabled.capacity() == 0);
    STAR_CHECK(!disabled.tryPush(1));
    STAR_CHECK(!disabled.tryPop());
}

STAR_TEST(spscQueueTransfersAndReleasesOwnedValues) {
    int live_count = 0;
    {
        star::SPSCQueue<TrackedValue> queue(1);
        STAR_CHECK(queue.tryEmplace(live_count));
        STAR_CHECK(live_count == 1);

        TrackedValue rejected(live_count);
        STAR_CHECK(!queue.tryPush(std::move(rejected)));
        STAR_CHECK(rejected.count == &live_count);

        auto value = queue.tryPop();
        STAR_CHECK(value.has_value() && live_count == 2);
        value.reset();
        STAR_CHECK(live_count == 1);
        STAR_CHECK(queue.tryPush(std::move(rejected)));
        STAR_CHECK(live_count == 1);
    }
    STAR_CHECK(live_count == 0);
}

STAR_TEST(spscQueuePreservesSlotAfterFailedConstruction) {
    star::SPSCQueue<CheckedValue> queue(1);
    bool rejected = false;
    try {
        static_cast<void>(queue.tryEmplace(-1));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    STAR_CHECK(rejected);
    STAR_CHECK(!queue.tryPop());
    STAR_CHECK(queue.tryEmplace(42));
    const auto result = queue.tryPop();
    STAR_CHECK(result.has_value() && result->value == 42);
}

STAR_TEST(spscQueueTransfersValuesAcrossThreads) {
    constexpr std::size_t k_count = 50'000;
    star::SPSCQueue<std::size_t> queue(17);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    std::jthread producer([&queue](std::stop_token stop) {
        for (std::size_t value = 0; value < k_count; ++value) {
            while (!queue.tryPush(value)) {
                if (stop.stop_requested()) {
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    for (std::size_t expected_value = 0; expected_value < k_count; ++expected_value) {
        auto value = queue.tryPop();
        while (!value) {
            STAR_CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
            value = queue.tryPop();
        }
        STAR_CHECK(*value == expected_value);
    }
    producer.join();
    STAR_CHECK(!queue.tryPop());
}

} // namespace
