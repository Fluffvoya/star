#include "core/linear_allocator.hpp"
#include "core/stack_allocator.hpp"

#include <bit>
#include <cstdint>

namespace star {

LinearAllocator::LinearAllocator(std::span<std::byte> storage) noexcept : m_storage(storage) {}

std::expected<void *, AllocationError> LinearAllocator::allocate(std::size_t size, std::size_t alignment) noexcept {
    if (size == 0) {
        return std::unexpected(AllocationError::InvalidSize);
    }
    if (!std::has_single_bit(alignment)) {
        return std::unexpected(AllocationError::InvalidAlignment);
    }

    const std::size_t remaining = m_storage.size() - m_offset;
    if (size > remaining) {
        return std::unexpected(AllocationError::Exhausted);
    }
    const auto address = reinterpret_cast<std::uintptr_t>(m_storage.data() + m_offset);
    const std::size_t padding = (std::uintptr_t{0} - address) & (alignment - 1);
    if (padding > remaining - size) {
        return std::unexpected(AllocationError::Exhausted);
    }

    m_offset += padding + size;
    return m_storage.data() + m_offset - size;
}

void LinearAllocator::reset() noexcept { m_offset = 0; }

std::size_t LinearAllocator::getUsedBytes() const noexcept { return m_offset; }

StackAllocator::StackAllocator(std::span<std::byte> storage) noexcept : m_linear(storage) {}

std::expected<void *, AllocationError> StackAllocator::allocate(std::size_t size, std::size_t alignment) noexcept {
    return m_linear.allocate(size, alignment);
}

StackAllocator::Marker StackAllocator::mark() const noexcept { return Marker(this, m_linear.getUsedBytes(), m_epoch); }

bool StackAllocator::rewind(Marker marker) noexcept {
    if (marker.m_owner != this || marker.m_epoch != m_epoch || marker.m_offset > m_linear.getUsedBytes()) {
        return false;
    }
    m_linear.m_offset = marker.m_offset;
    return true;
}

void StackAllocator::reset() noexcept {
    m_linear.reset();
    ++m_epoch;
}

} // namespace star
