#include "test_support.hpp"

#include "core/delegate.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <thread>

namespace {

STAR_TEST(delegateBindsRebindsAndSupportsWeakMemberTargets) {
    star::Delegate<int(int)> delegate;
    STAR_CHECK(!delegate.isBound());
    STAR_CHECK(delegate(3) == 0);

    delegate.bind([](int value) { return value + 2; });
    STAR_CHECK(delegate.isBound() && delegate(3) == 5);
    delegate.bind([](int value) { return value * 2; });
    STAR_CHECK(delegate(3) == 6);
    delegate.unbind();
    STAR_CHECK(!delegate.isBound() && delegate(3) == 0);

    struct Receiver {
        int call(int value) { return value + 10; }
    };
    auto receiver = std::make_shared<Receiver>();
    delegate.bindMember(receiver, &Receiver::call);
    STAR_CHECK(delegate(2) == 12);
    receiver.reset();
    STAR_CHECK(delegate(2) == 0);
}

STAR_TEST(delegateCopiesShareCallableState) {
    int calls = 0;
    star::Delegate<void(int)> first;
    first.bind([&](int value) { calls += value; });
    auto second = first;
    second(4);
    STAR_CHECK(calls == 4);
    first.unbind();
    second(4);
    STAR_CHECK(calls == 4);
}

STAR_TEST(multicastDelegateAddsRemovesAndSupportsMutationDuringBroadcast) {
    star::MulticastDelegate<void(int)> delegate;
    int first_calls = 0;
    int second_calls = 0;
    star::DelegateHandle second_handle;
    const auto first_handle = delegate.add([&](int value) {
        first_calls += value;
        delegate.remove(second_handle);
    });
    second_handle = delegate.add([&](int value) { second_calls += value; });
    STAR_CHECK(first_handle.isValid() && second_handle.isValid() && delegate.getSize() == 2);

    delegate.broadcast(3);
    STAR_CHECK(first_calls == 3 && second_calls == 3);
    STAR_CHECK(delegate.getSize() == 1);
    STAR_CHECK(!delegate.remove(second_handle));
    STAR_CHECK(delegate.remove(first_handle));
    STAR_CHECK(delegate.getSize() == 0);
}

STAR_TEST(multicastDelegateUsesSnapshotAndSupportsConcurrentAccess) {
    star::MulticastDelegate<void()> delegate;
    std::atomic_int calls = 0;
    const auto handle = delegate.add([&] { calls.fetch_add(1, std::memory_order_relaxed); });
    STAR_CHECK(handle.isValid());
    {
        std::array<std::jthread, 4> threads;
        for (auto &thread : threads) {
            thread = std::jthread([&] {
                for (int iteration = 0; iteration < 100; ++iteration) {
                    delegate.broadcast();
                }
            });
        }
        std::jthread mutator([&] {
            for (int iteration = 0; iteration < 100; ++iteration) {
                delegate.remove(handle);
                static_cast<void>(delegate.add([&] { calls.fetch_add(1, std::memory_order_relaxed); }));
            }
        });
    }
    STAR_CHECK(calls.load(std::memory_order_relaxed) > 0);
    delegate.clear();
    STAR_CHECK(delegate.getSize() == 0);
}

} // namespace
