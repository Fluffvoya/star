#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace star {

// A bounded queue with one producer and one consumer. End both users before destruction.
template <class T>
    requires std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>
class SPSCQueue final {
  public:
    explicit SPSCQueue(std::size_t capacity) : m_slots(slotCount(capacity)) {}

    SPSCQueue(const SPSCQueue &) = delete;
    SPSCQueue &operator=(const SPSCQueue &) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return m_slots.size() - 1; }

    // Producer only. Arguments are consumed only when there is room for an element.
    template <class... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] bool tryEmplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const std::size_t write = m_write.load(std::memory_order_relaxed);
        const std::size_t next_write = nextIndex(write);
        if (next_write == m_read.load(std::memory_order_acquire)) {
            return false;
        }

        m_slots[write].emplace(std::forward<Args>(args)...);
        m_write.store(next_write, std::memory_order_release);
        return true;
    }

    template <class U>
        requires std::constructible_from<T, U>
    [[nodiscard]] bool tryPush(U &&value) noexcept(std::is_nothrow_constructible_v<T, U>) {
        return tryEmplace(std::forward<U>(value));
    }

    // Consumer only. The producer may reuse a slot once its value has been destroyed.
    [[nodiscard]] std::optional<T> tryPop() noexcept {
        const std::size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        std::optional<T> result(std::in_place, std::move(*m_slots[read]));
        m_slots[read].reset();
        m_read.store(nextIndex(read), std::memory_order_release);
        return result;
    }

  private:
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

    [[nodiscard]] static std::size_t slotCount(std::size_t capacity) {
        if (capacity == std::numeric_limits<std::size_t>::max()) {
            throw std::length_error("SPSCQueue capacity is too large");
        }
        return capacity + 1;
    }

    [[nodiscard]] std::size_t nextIndex(std::size_t index) const noexcept {
        ++index;
        return index == m_slots.size() ? 0 : index;
    }

    std::vector<std::optional<T>> m_slots;
    std::atomic<std::size_t> m_read{0};
    std::atomic<std::size_t> m_write{0};
};

} // namespace star
