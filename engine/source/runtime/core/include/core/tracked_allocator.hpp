#pragma once

#include "core/allocation_error.hpp"
#include "core/export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>

namespace star {

enum class MemTag : std::uint8_t {
    Core,
    Renderer,
    Physics,
    Count,
};

struct AllocationStats {
    std::size_t live_bytes = 0;
    std::size_t peak_bytes = 0;
    std::size_t live_allocations = 0;
};

class STAR_CORE_API TrackedAllocator final {
  public:
    using AllocateFunction = void *(void *context, std::size_t size, std::size_t alignment) noexcept;
    using DeallocateFunction = void(void *context, void *address, std::size_t size, std::size_t alignment) noexcept;

    TrackedAllocator() noexcept;

    // The upstream must outlive the wrapper and support concurrent calls when shared between threads.
    TrackedAllocator(void *context, AllocateFunction &allocate, DeallocateFunction &deallocate) noexcept;

    [[nodiscard]] std::expected<void *, AllocationError>
    allocate(std::size_t size, MemTag tag, std::size_t alignment = alignof(std::max_align_t)) noexcept;

    // Pair each allocation with one deallocation using the original size, tag, and alignment.
    void deallocate(void *address, std::size_t size, MemTag tag,
                    std::size_t alignment = alignof(std::max_align_t)) noexcept;

    [[nodiscard]] std::expected<AllocationStats, AllocationError> getStats(MemTag tag) const noexcept;

  private:
    void *m_context;
    AllocateFunction *m_allocate;
    DeallocateFunction *m_deallocate;
    mutable std::mutex m_mutex;
    std::array<AllocationStats, static_cast<std::size_t>(MemTag::Count)> m_stats{};
};

} // namespace star
