#include "test_support.hpp"

#include "core/global_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

class EnvironmentGuard final {
  public:
    explicit EnvironmentGuard(const char *value) {
        const char *old_value = std::getenv("STAR_ALLOCATOR");
        if (old_value != nullptr) {
            m_old_value = old_value;
        }
        if (value == nullptr) {
            unsetenv("STAR_ALLOCATOR");
        } else {
            setenv("STAR_ALLOCATOR", value, 1);
        }
    }

    ~EnvironmentGuard() {
        if (m_old_value.empty()) {
            unsetenv("STAR_ALLOCATOR");
        } else {
            setenv("STAR_ALLOCATOR", m_old_value.c_str(), 1);
        }
    }

  private:
    std::string m_old_value;
};

STAR_TEST(globalAllocatorDefaultsToSystemAndValidatesEnvironment) {
    {
        const EnvironmentGuard guard(nullptr);
        const auto allocator = star::GlobalAllocator::fromEnvironment();
        STAR_CHECK(allocator && allocator->getKind() == star::GlobalAllocatorKind::System);
        STAR_CHECK(std::string_view(allocator->getName()) == "system");
    }
    {
        const EnvironmentGuard guard("system");
        const auto allocator = star::GlobalAllocator::fromEnvironment();
        STAR_CHECK(allocator && allocator->getKind() == star::GlobalAllocatorKind::System);
    }
    {
        const EnvironmentGuard guard("unknown");
        const auto allocator = star::GlobalAllocator::fromEnvironment();
        STAR_CHECK(!allocator && allocator.error() == star::GlobalAllocatorError::InvalidEnvironment);
    }
    {
        const EnvironmentGuard guard("mimalloc");
        const auto allocator = star::GlobalAllocator::fromEnvironment();
#if defined(STAR_WITH_MIMALLOC)
        STAR_CHECK(allocator && allocator->getKind() == star::GlobalAllocatorKind::Mimalloc);
#else
        STAR_CHECK(!allocator && allocator.error() == star::GlobalAllocatorError::Unavailable);
#endif
    }
}

STAR_TEST(globalAllocatorAllocatesAlignedMemoryAndHandlesFailures) {
    const star::GlobalAllocator allocator;
    const auto allocation = allocator.allocate(128, 64);
    STAR_CHECK(allocation);
    STAR_CHECK(reinterpret_cast<std::uintptr_t>(*allocation) % 64 == 0);
    allocator.deallocate(*allocation, 64);

    const auto invalid_size = allocator.allocate(0);
    const auto invalid_alignment = allocator.allocate(1, 3);
    STAR_CHECK(!invalid_size && invalid_size.error() == star::GlobalAllocatorError::InvalidSize);
    STAR_CHECK(!invalid_alignment && invalid_alignment.error() == star::GlobalAllocatorError::InvalidAlignment);
}

} // namespace
