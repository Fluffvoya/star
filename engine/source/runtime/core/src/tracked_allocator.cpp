#include "core/tracked_allocator.hpp"

#include "core/assert.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>

namespace star {

namespace {

void *allocateSystem(void *, std::size_t size, std::size_t alignment) noexcept {
    if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::nothrow);
    }
    return ::operator new(size, std::align_val_t(alignment), std::nothrow);
}

void deallocateSystem(void *, void *address, std::size_t, std::size_t alignment) noexcept {
    if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(address);
    } else {
        ::operator delete(address, std::align_val_t(alignment));
    }
}

} // namespace

TrackedAllocator::TrackedAllocator() noexcept : TrackedAllocator(nullptr, allocateSystem, deallocateSystem) {}

TrackedAllocator::TrackedAllocator(void *context, AllocateFunction &allocate, DeallocateFunction &deallocate) noexcept
    : m_context(context), m_allocate(&allocate), m_deallocate(&deallocate) {}

std::expected<void *, AllocationError> TrackedAllocator::allocate(std::size_t size, MemTag tag,
                                                                  std::size_t alignment) noexcept {
    if (size == 0) {
        return std::unexpected(AllocationError::InvalidSize);
    }
    if (!std::has_single_bit(alignment) || alignment > std::numeric_limits<std::ptrdiff_t>::max()) {
        return std::unexpected(AllocationError::InvalidAlignment);
    }
    const auto index = static_cast<std::size_t>(tag);
    if (index >= m_stats.size()) {
        return std::unexpected(AllocationError::InvalidTag);
    }

    void *address = m_allocate(m_context, size, alignment);
    if (address == nullptr) {
        return std::unexpected(AllocationError::Exhausted);
    }

    const std::lock_guard lock(m_mutex);
    AllocationStats &stats = m_stats[index];
    stats.live_bytes += size;
    stats.peak_bytes = std::max(stats.peak_bytes, stats.live_bytes);
    ++stats.live_allocations;
    return address;
}

void TrackedAllocator::deallocate(void *address, std::size_t size, MemTag tag, std::size_t alignment) noexcept {
    if (address == nullptr) {
        return;
    }
    const auto index = static_cast<std::size_t>(tag);
    if (index >= m_stats.size()) {
        STAR_FATAL("Tracked allocation has an invalid memory tag");
    }
    {
        const std::lock_guard lock(m_mutex);
        AllocationStats &stats = m_stats[index];
        if (stats.live_bytes < size || stats.live_allocations == 0) {
            STAR_FATAL("Tracked deallocation has no matching allocation");
        }
        stats.live_bytes -= size;
        --stats.live_allocations;
    }
    m_deallocate(m_context, address, size, alignment);
}

std::expected<AllocationStats, AllocationError> TrackedAllocator::getStats(MemTag tag) const noexcept {
    const auto index = static_cast<std::size_t>(tag);
    if (index >= m_stats.size()) {
        return std::unexpected(AllocationError::InvalidTag);
    }
    const std::lock_guard lock(m_mutex);
    return m_stats[index];
}

} // namespace star
