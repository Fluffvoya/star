#pragma once

#include "core/allocation_error.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace star {

// Borrows exclusive storage and owns the objects it constructs in that storage.
template <class T> class PoolAllocator final {
    static_assert(std::is_nothrow_destructible_v<T>);

    static constexpr std::size_t end_of_list = std::numeric_limits<std::size_t>::max();

  public:
    class Storage final {
      public:
        Storage() = default;
        Storage(const Storage &) = delete;
        Storage &operator=(const Storage &) = delete;

      private:
        friend PoolAllocator;

        [[nodiscard]] T *getObject() noexcept { return std::launder(reinterpret_cast<T *>(m_bytes)); }

        alignas(T) std::byte m_bytes[sizeof(T)];
        std::size_t m_next = end_of_list;
        bool m_live = false;
    };

    explicit PoolAllocator(std::span<Storage> storage) noexcept : m_storage(storage) {
        for (std::size_t index = storage.size(); index != 0; --index) {
            storage[index - 1].m_next = m_free_head;
            m_free_head = index - 1;
        }
    }

    PoolAllocator(const PoolAllocator &) = delete;
    PoolAllocator &operator=(const PoolAllocator &) = delete;

    ~PoolAllocator() {
        for (Storage &slot : m_storage) {
            if (slot.m_live) {
                std::destroy_at(slot.getObject());
                slot.m_live = false;
            }
        }
    }

    template <class... Args> [[nodiscard]] std::expected<T *, AllocationError> create(Args &&...args) noexcept {
        if (m_free_head == end_of_list) {
            return std::unexpected(AllocationError::Exhausted);
        }
        const std::size_t index = m_free_head;
        Storage &slot = m_storage[index];
        m_free_head = slot.m_next;

        try {
            T *object = std::construct_at(reinterpret_cast<T *>(slot.m_bytes), std::forward<Args>(args)...);
            slot.m_live = true;
            return object;
        } catch (...) {
            slot.m_next = m_free_head;
            m_free_head = index;
            return std::unexpected(AllocationError::ConstructionFailed);
        }
    }

    // Accepts only a currently live object from this pool. Discard all borrowed pointers after destruction.
    bool destroy(T *object) noexcept {
        if (object == nullptr || m_storage.empty()) {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(m_storage.front().m_bytes);
        const auto address = reinterpret_cast<std::uintptr_t>(object);
        if (address < base || (address - base) % sizeof(Storage) != 0) {
            return false;
        }
        const std::size_t index = (address - base) / sizeof(Storage);
        if (index >= m_storage.size() || !m_storage[index].m_live) {
            return false;
        }

        Storage &slot = m_storage[index];
        slot.m_live = false;
        std::destroy_at(object);
        slot.m_next = m_free_head;
        m_free_head = index;
        return true;
    }

  private:
    std::span<Storage> m_storage;
    std::size_t m_free_head = end_of_list;
};

} // namespace star
