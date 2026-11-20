#include "core/global_allocator.hpp"

#include <cstddef>
#include <new>

#if defined(STAR_MONOLITHIC)
namespace {

star::GlobalAllocator selectAllocator() noexcept {
    const auto configured = star::GlobalAllocator::fromEnvironment();
    if (configured) {
        return *configured;
    }
    return star::GlobalAllocator{};
}

const star::GlobalAllocator &globalAllocator() noexcept {
    static const star::GlobalAllocator allocator = selectAllocator();
    return allocator;
}

void *allocate(std::size_t size, std::size_t alignment) {
    const auto allocation = globalAllocator().allocate(size == 0 ? 1 : size, alignment);
    if (!allocation || *allocation == nullptr) {
        throw std::bad_alloc();
    }
    return *allocation;
}

} // namespace

void *operator new(std::size_t size) { return allocate(size, alignof(std::max_align_t)); }
void *operator new[](std::size_t size) { return allocate(size, alignof(std::max_align_t)); }

void *operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return allocate(size, alignof(std::max_align_t));
    } catch (...) {
        return nullptr;
    }
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    try {
        return allocate(size, alignof(std::max_align_t));
    } catch (...) {
        return nullptr;
    }
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    try {
        return allocate(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    try {
        return allocate(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void *address) noexcept { globalAllocator().deallocate(address); }
void operator delete[](void *address) noexcept { globalAllocator().deallocate(address); }
void operator delete(void *address, std::size_t) noexcept { globalAllocator().deallocate(address); }
void operator delete[](void *address, std::size_t) noexcept { globalAllocator().deallocate(address); }

void operator delete(void *address, std::align_val_t alignment) noexcept {
    globalAllocator().deallocate(address, static_cast<std::size_t>(alignment));
}

void operator delete[](void *address, std::align_val_t alignment) noexcept {
    globalAllocator().deallocate(address, static_cast<std::size_t>(alignment));
}

void operator delete(void *address, std::size_t, std::align_val_t alignment) noexcept {
    globalAllocator().deallocate(address, static_cast<std::size_t>(alignment));
}

void operator delete[](void *address, std::size_t, std::align_val_t alignment) noexcept {
    globalAllocator().deallocate(address, static_cast<std::size_t>(alignment));
}
#endif
