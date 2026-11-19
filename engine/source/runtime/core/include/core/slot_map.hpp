#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace star {

enum class SlotMapError : std::uint8_t {
    CapacityExceeded,
    AllocationFailed,
    ConstructionFailed,
};

// Handles belong to their originating map. Structural changes may invalidate borrowed pointers.
template <class T> class SlotMap final {
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                  std::is_nothrow_destructible_v<T>);

    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

  public:
    class Handle final {
      public:
        constexpr Handle() = default;

        [[nodiscard]] constexpr std::uint32_t getIndex() const noexcept { return m_index; }
        [[nodiscard]] constexpr std::uint32_t getGeneration() const noexcept { return m_generation; }

        friend constexpr bool operator==(Handle, Handle) = default;

      private:
        friend SlotMap;

        constexpr Handle(std::uint32_t index, std::uint32_t generation) : m_index(index), m_generation(generation) {}

        std::uint32_t m_index = invalid_index;
        std::uint32_t m_generation = 0;
    };

    SlotMap() = default;
    SlotMap(const SlotMap &) = delete;
    SlotMap &operator=(const SlotMap &) = delete;

    template <class... Args> [[nodiscard]] std::expected<Handle, SlotMapError> emplace(Args &&...args) noexcept {
        const bool needs_slot = m_free_head == invalid_index;
        if (m_dense.size() == m_dense.max_size() ||
            (needs_slot && (m_slots.size() == invalid_index || m_slots.size() == m_slots.max_size()))) {
            return std::unexpected(SlotMapError::CapacityExceeded);
        }

        const auto slot_index = needs_slot ? static_cast<std::uint32_t>(m_slots.size()) : m_free_head;
        if (needs_slot) {
            try {
                m_slots.emplace_back();
            } catch (const std::bad_alloc &) {
                return std::unexpected(SlotMapError::AllocationFailed);
            }
        }

        const auto dense_index = static_cast<std::uint32_t>(m_dense.size());
        try {
            m_dense.emplace_back(slot_index, std::forward<Args>(args)...);
        } catch (const std::bad_alloc &) {
            if (needs_slot) {
                m_slots.pop_back();
            }
            return std::unexpected(SlotMapError::AllocationFailed);
        } catch (...) {
            if (needs_slot) {
                m_slots.pop_back();
            }
            return std::unexpected(SlotMapError::ConstructionFailed);
        }

        Slot &slot = m_slots[slot_index];
        if (!needs_slot) {
            m_free_head = slot.next_free;
        }
        slot.dense_index = dense_index;
        return Handle(slot_index, slot.generation);
    }

    [[nodiscard]] bool contains(Handle handle) const noexcept {
        return handle.m_index < m_slots.size() && m_slots[handle.m_index].dense_index != invalid_index &&
               m_slots[handle.m_index].generation == handle.m_generation;
    }

    [[nodiscard]] T *get(Handle handle) noexcept {
        return contains(handle) ? std::addressof(m_dense[m_slots[handle.m_index].dense_index].value) : nullptr;
    }

    [[nodiscard]] const T *get(Handle handle) const noexcept {
        return contains(handle) ? std::addressof(m_dense[m_slots[handle.m_index].dense_index].value) : nullptr;
    }

    [[nodiscard]] std::size_t getSize() const noexcept { return m_dense.size(); }

    [[nodiscard]] Handle getHandleAt(std::size_t dense_index) const noexcept {
        if (dense_index >= m_dense.size()) {
            return {};
        }
        const std::uint32_t slot_index = m_dense[dense_index].slot_index;
        return Handle(slot_index, m_slots[slot_index].generation);
    }

    bool erase(Handle handle) noexcept {
        if (!contains(handle)) {
            return false;
        }

        Slot &slot = m_slots[handle.m_index];
        if (slot.dense_index != m_dense.size() - 1) {
            m_dense[slot.dense_index] = std::move(m_dense.back());
            m_slots[m_dense[slot.dense_index].slot_index].dense_index = slot.dense_index;
        }
        m_dense.pop_back();
        slot.dense_index = invalid_index;

        // Retire the slot before the generation can wrap and revive an old handle.
        if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
            ++slot.generation;
            slot.next_free = m_free_head;
            m_free_head = handle.m_index;
        }
        return true;
    }

    void clear() noexcept {
        while (!m_dense.empty()) {
            erase(getHandleAt(m_dense.size() - 1));
        }
    }

  private:
    struct Slot {
        std::uint32_t dense_index = invalid_index;
        std::uint32_t next_free = invalid_index;
        std::uint32_t generation = 1;
    };

    struct Entry {
        template <class... Args>
        Entry(std::uint32_t index, Args &&...args) : value(std::forward<Args>(args)...), slot_index(index) {}

        T value;
        std::uint32_t slot_index;
    };

    std::vector<Slot> m_slots;
    std::vector<Entry> m_dense;
    std::uint32_t m_free_head = invalid_index;
};

} // namespace star
