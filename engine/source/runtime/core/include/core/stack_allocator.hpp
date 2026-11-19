#pragma once

#include "core/linear_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace star {

// Markers are scoped to this allocator. Discard markers into storage reclaimed by rewind or reset.
class STAR_CORE_API StackAllocator final {
  public:
    class Marker final {
      private:
        friend class StackAllocator;

        Marker(const StackAllocator *owner, std::size_t offset, std::uint64_t epoch) noexcept
            : m_owner(owner), m_offset(offset), m_epoch(epoch) {}

        const StackAllocator *m_owner;
        std::size_t m_offset;
        std::uint64_t m_epoch;
    };

    explicit StackAllocator(std::span<std::byte> storage) noexcept;

    [[nodiscard]] std::expected<void *, AllocationError>
    allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;
    [[nodiscard]] Marker mark() const noexcept;
    bool rewind(Marker marker) noexcept;
    void reset() noexcept;

  private:
    LinearAllocator m_linear;
    std::uint64_t m_epoch = 0;
};

} // namespace star
