#pragma once

#include "core/allocation_error.hpp"
#include "core/export.hpp"

#include <cstddef>
#include <expected>
#include <span>

namespace star {

// Borrows exclusive storage. The caller manages object lifetimes and thread ownership.
class STAR_CORE_API LinearAllocator final {
  public:
    explicit LinearAllocator(std::span<std::byte> storage) noexcept;
    LinearAllocator(const LinearAllocator &) = delete;
    LinearAllocator &operator=(const LinearAllocator &) = delete;

    [[nodiscard]] std::expected<void *, AllocationError>
    allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;

    void reset() noexcept;

    [[nodiscard]] std::size_t getUsedBytes() const noexcept;

  private:
    friend class StackAllocator;

    std::span<std::byte> m_storage;
    std::size_t m_offset = 0;
};

} // namespace star
