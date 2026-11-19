#pragma once

#include "core/allocation_error.hpp"
#include "core/export.hpp"

#include <cstddef>
#include <expected>

namespace star {

enum class GlobalAllocatorKind { System, Mimalloc };

enum class GlobalAllocatorError { InvalidSize, InvalidAlignment, InvalidEnvironment, Unavailable };

class STAR_CORE_API GlobalAllocator final {
  public:
    GlobalAllocator() noexcept = default;

    [[nodiscard]] static std::expected<GlobalAllocator, GlobalAllocatorError> fromEnvironment() noexcept;

    [[nodiscard]] GlobalAllocatorKind getKind() const noexcept { return m_kind; }
    [[nodiscard]] const char *getName() const noexcept;

    [[nodiscard]] std::expected<void *, GlobalAllocatorError>
    allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) const noexcept;

    void deallocate(void *address, std::size_t alignment = alignof(std::max_align_t)) const noexcept;

  private:
    explicit GlobalAllocator(GlobalAllocatorKind kind) noexcept : m_kind(kind) {}

    GlobalAllocatorKind m_kind = GlobalAllocatorKind::System;
};

} // namespace star
