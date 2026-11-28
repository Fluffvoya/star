#pragma once

#include "rhi/rhi.hpp"

#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace star::rhi {

template <typename T, typename HandleType> class ResourcePool final {
  public:
    [[nodiscard]] std::expected<HandleType, DeviceError> emplace(T value) noexcept {
        try {
            for (std::uint32_t index = 0; index < m_slots.size(); ++index) {
                Slot &slot = m_slots[index];
                if (!slot.value.has_value() && !slot.pendingDestroy) {
                    slot.value.emplace(std::move(value));
                    return HandleType(index, slot.generation);
                }
            }
            if (m_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(DeviceError::OutOfMemory);
            }
            m_slots.push_back(Slot{});
            Slot &slot = m_slots.back();
            slot.value.emplace(std::move(value));
            return HandleType(static_cast<std::uint32_t>(m_slots.size() - 1), slot.generation);
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    [[nodiscard]] T *get(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        return slot != nullptr && slot->value ? &*slot->value : nullptr;
    }

    [[nodiscard]] const T *get(HandleType handle) const noexcept {
        const Slot *slot = getSlot(handle);
        return slot != nullptr && slot->value ? &*slot->value : nullptr;
    }

    void reclaimMarked() noexcept {
        for (std::uint32_t index = 0; index < m_slots.size(); ++index) {
            if (m_slots[index].pendingDestroy) {
                reclaim(HandleType(index, m_slots[index].generation));
            }
        }
    }

    [[nodiscard]] bool contains(HandleType handle) const noexcept {
        const Slot *slot = getSlot(handle);
        return slot != nullptr && slot->value.has_value();
    }

    [[nodiscard]] bool markForDestroy(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value() || slot->pendingDestroy) {
            return false;
        }
        slot->pendingDestroy = true;
        return true;
    }

    bool cancelDestroy(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value() || !slot->pendingDestroy) {
            return false;
        }
        slot->pendingDestroy = false;
        return true;
    }

    bool reclaim(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value() || !slot->pendingDestroy) {
            return false;
        }
        slot->value.reset();
        slot->pendingDestroy = false;
        ++slot->generation;
        if (slot->generation == 0) {
            slot->generation = 1;
        }
        return true;
    }

    // Drops a value immediately. Callers must keep no in-flight GPU work referencing it.
    bool erase(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value()) {
            return false;
        }
        slot->value.reset();
        slot->pendingDestroy = false;
        ++slot->generation;
        if (slot->generation == 0) {
            slot->generation = 1;
        }
        return true;
    }

  private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 1;
        bool pendingDestroy = false;
    };

    [[nodiscard]] Slot *getSlot(HandleType handle) noexcept {
        if (!handle.isValid() || handle.getIndex() >= m_slots.size()) {
            return nullptr;
        }
        Slot &slot = m_slots[handle.getIndex()];
        return slot.generation == handle.getGeneration() ? &slot : nullptr;
    }

    [[nodiscard]] const Slot *getSlot(HandleType handle) const noexcept {
        if (!handle.isValid() || handle.getIndex() >= m_slots.size()) {
            return nullptr;
        }
        const Slot &slot = m_slots[handle.getIndex()];
        return slot.generation == handle.getGeneration() ? &slot : nullptr;
    }

    std::vector<Slot> m_slots;
};

} // namespace star::rhi
