#include "core/global_allocator.hpp"

#include <bit>
#include <cstdlib>
#include <limits>
#include <new>
#include <string_view>

namespace star {

std::expected<GlobalAllocator, GlobalAllocatorError> GlobalAllocator::fromEnvironment() noexcept {
    const char *value = std::getenv("STAR_ALLOCATOR");
    if (value == nullptr || std::string_view(value) == "system") {
        return GlobalAllocator(GlobalAllocatorKind::System);
    }
    if (std::string_view(value) == "mimalloc") {
#if defined(STAR_WITH_MIMALLOC)
        return GlobalAllocator(GlobalAllocatorKind::Mimalloc);
#else
        return std::unexpected(GlobalAllocatorError::Unavailable);
#endif
    }
    return std::unexpected(GlobalAllocatorError::InvalidEnvironment);
}

const char *GlobalAllocator::getName() const noexcept {
    return m_kind == GlobalAllocatorKind::System ? "system" : "mimalloc";
}

std::expected<void *, GlobalAllocatorError> GlobalAllocator::allocate(std::size_t size,
                                                                      std::size_t alignment) const noexcept {
    if (size == 0) {
        return std::unexpected(GlobalAllocatorError::InvalidSize);
    }
    if (!std::has_single_bit(alignment) || alignment > std::numeric_limits<std::size_t>::max()) {
        return std::unexpected(GlobalAllocatorError::InvalidAlignment);
    }

#if defined(STAR_WITH_MIMALLOC)
    if (m_kind == GlobalAllocatorKind::Mimalloc) {
        return mi_malloc_aligned(size, alignment);
    }
#endif
    if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::nothrow);
    }
    return ::operator new(size, std::align_val_t(alignment), std::nothrow);
}

void GlobalAllocator::deallocate(void *address, std::size_t alignment) const noexcept {
    if (address == nullptr) {
        return;
    }
#if defined(STAR_WITH_MIMALLOC)
    if (m_kind == GlobalAllocatorKind::Mimalloc) {
        mi_free(address);
        return;
    }
#endif
    if (alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(address);
    } else {
        ::operator delete(address, std::align_val_t(alignment));
    }
}

} // namespace star
