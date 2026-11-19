#include "test_support.hpp"

#include "core/core.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

STAR_TEST(guidGenerateProducesValidValue) {
    const star::Guid guid = star::Guid::generate();

    STAR_CHECK(guid.isValid());
    STAR_CHECK(guid.toString().size() == 32);
    STAR_CHECK((guid.getBytes()[6] & 0xf0U) == 0x40U);
    STAR_CHECK((guid.getBytes()[8] & 0xc0U) == 0x80U);
}

STAR_TEST(guidParseRoundTripsText) {
    const star::Guid original = star::Guid::generate();
    const auto parsed = star::Guid::parse(original.toString());

    STAR_CHECK(parsed.has_value());
    STAR_CHECK(*parsed == original);
}

STAR_TEST(guidParseRejectsInvalidInput) {
    const auto short_value = star::Guid::parse("0123");
    const auto invalid_value = star::Guid::parse("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");

    STAR_CHECK(!short_value.has_value());
    STAR_CHECK(short_value.error() == star::GuidParseError::InvalidLength);
    STAR_CHECK(!invalid_value.has_value());
    STAR_CHECK(invalid_value.error() == star::GuidParseError::InvalidCharacter);
}

STAR_TEST(nameInternsAndComparesById) {
    const star::Name first("PlayerCharacter");
    const star::Name second("PlayerCharacter");
    const star::Name different("Camera");

    STAR_CHECK(first == second);
    STAR_CHECK(first != different);
    STAR_CHECK(first.getId() == second.getId());
    STAR_CHECK(first.getString() == "PlayerCharacter");
    STAR_CHECK(star::Name{}.isEmpty());
}

STAR_TEST(slotMapKeepsHandlesAcrossDenseMovesAndRejectsStaleHandles) {
    star::SlotMap<std::unique_ptr<int>> map;
    const auto first = map.emplace(std::make_unique<int>(1));
    const auto middle = map.emplace(std::make_unique<int>(2));
    const auto last = map.emplace(std::make_unique<int>(3));
    STAR_CHECK(first && middle && last);

    STAR_CHECK(map.erase(*middle));
    STAR_CHECK(map.getSize() == 2);
    STAR_CHECK(map.get(*middle) == nullptr);
    STAR_CHECK(!map.erase(*middle));
    STAR_CHECK(map.getHandleAt(1) == *last);
    STAR_CHECK(**std::as_const(map).get(*last) == 3);
    STAR_CHECK(**map.get(*first) == 1);

    const auto reused = map.emplace(std::make_unique<int>(4));
    STAR_CHECK(reused);
    STAR_CHECK(reused->getIndex() == middle->getIndex());
    STAR_CHECK(reused->getGeneration() != middle->getGeneration());
    STAR_CHECK(!map.contains(*middle));

    map.clear();
    STAR_CHECK(map.getSize() == 0);
    STAR_CHECK(!map.contains(*first));
    STAR_CHECK(!map.contains(*last));
    STAR_CHECK(!map.contains(*reused));
    STAR_CHECK(!map.contains(map.getHandleAt(0)));
}

struct SlotValue {
    explicit SlotValue(bool succeed) {
        if (!succeed) {
            throw std::runtime_error("construction failed");
        }
    }
};

STAR_TEST(slotMapRetainsFreeSlotsAfterConstructionFailure) {
    star::SlotMap<SlotValue> map;
    const auto initial_error = map.emplace(false);
    STAR_CHECK(!initial_error && initial_error.error() == star::SlotMapError::ConstructionFailed);
    STAR_CHECK(map.getSize() == 0);
    const auto first = map.emplace(true);
    STAR_CHECK(first && first->getIndex() == 0);
    STAR_CHECK(map.erase(*first));

    const auto reuse_error = map.emplace(false);
    STAR_CHECK(!reuse_error && reuse_error.error() == star::SlotMapError::ConstructionFailed);
    const auto reused = map.emplace(true);
    STAR_CHECK(reused && reused->getIndex() == first->getIndex());
    STAR_CHECK(map.getSize() == 1);
    STAR_CHECK(!map.contains(*first));
}

STAR_TEST(linearAllocatorAlignsAndPreservesCursorOnFailure) {
    alignas(32) std::array<std::byte, 96> storage{};
    star::LinearAllocator allocator{std::span(storage).subspan(1)};
    const auto first = allocator.allocate(1, 1);
    const auto aligned = allocator.allocate(16, 32);
    STAR_CHECK(first && *first == storage.data() + 1);
    STAR_CHECK(aligned && *aligned == storage.data() + 32);

    const std::size_t used = allocator.getUsedBytes();
    const auto exhausted = allocator.allocate(50, 1);
    const auto unaligned = allocator.allocate(1, 3);
    const auto empty = allocator.allocate(0);
    STAR_CHECK(!exhausted && exhausted.error() == star::AllocationError::Exhausted);
    STAR_CHECK(!unaligned && unaligned.error() == star::AllocationError::InvalidAlignment);
    STAR_CHECK(!empty && empty.error() == star::AllocationError::InvalidSize);
    STAR_CHECK(allocator.getUsedBytes() == used);

    allocator.reset();
    STAR_CHECK(allocator.getUsedBytes() == 0);
    STAR_CHECK(allocator.allocate(1, 1) == first);
}

STAR_TEST(stackAllocatorRewindsNestedMarkersAndRejectsForeignMarkers) {
    alignas(16) std::array<std::byte, 64> storage{};
    std::array<std::byte, 16> other_storage{};
    star::StackAllocator allocator(storage);
    star::StackAllocator other(other_storage);

    const auto root = allocator.mark();
    STAR_CHECK(allocator.allocate(1, 1));
    const auto nested = allocator.mark();
    const auto second = allocator.allocate(8, 16);
    STAR_CHECK(second);
    STAR_CHECK(!allocator.rewind(other.mark()));
    STAR_CHECK(allocator.rewind(nested));
    STAR_CHECK(allocator.allocate(8, 16) == second);
    STAR_CHECK(allocator.rewind(root));
    STAR_CHECK(!allocator.rewind(nested));
    allocator.reset();
    STAR_CHECK(!allocator.rewind(root));
}

struct alignas(64) PoolValue {
    explicit PoolValue(int &count, bool succeed = true) : live_count(count) {
        if (!succeed) {
            throw std::runtime_error("construction failed");
        }
        ++live_count;
    }

    ~PoolValue() { --live_count; }

    int &live_count;
};

STAR_TEST(poolAllocatorReusesSlotsAndDestroysRemainingObjects) {
    int live_count = 0;
    std::array<star::PoolAllocator<PoolValue>::Storage, 2> storage;
    {
        star::PoolAllocator<PoolValue> pool(storage);
        PoolValue foreign(live_count);
        STAR_CHECK(!pool.destroy(&foreign));
        const auto construction_error = pool.create(live_count, false);
        STAR_CHECK(!construction_error && construction_error.error() == star::AllocationError::ConstructionFailed);

        const auto first = pool.create(live_count);
        const auto second = pool.create(live_count);
        STAR_CHECK(first && second && *first != *second);
        STAR_CHECK(reinterpret_cast<std::uintptr_t>(*first) % alignof(PoolValue) == 0);
        STAR_CHECK(live_count == 3);
        const auto exhausted = pool.create(live_count);
        STAR_CHECK(!exhausted && exhausted.error() == star::AllocationError::Exhausted);

        STAR_CHECK(pool.destroy(*first));
        STAR_CHECK(!pool.destroy(*first));
        STAR_CHECK(pool.create(live_count) == first);
        STAR_CHECK(live_count == 3);
    }
    STAR_CHECK(live_count == 0);
}

STAR_TEST(trackedAllocatorKeepsSeparateLiveAndPeakStats) {
    star::TrackedAllocator allocator;
    const auto physics = allocator.allocate(32, star::MemTag::Physics, 64);
    const auto renderer = allocator.allocate(24, star::MemTag::Renderer);
    STAR_CHECK(physics && renderer);
    STAR_CHECK(reinterpret_cast<std::uintptr_t>(*physics) % 64 == 0);

    const auto active = allocator.getStats(star::MemTag::Physics);
    STAR_CHECK(active && active->live_bytes == 32 && active->peak_bytes == 32 && active->live_allocations == 1);
    allocator.deallocate(*physics, 32, star::MemTag::Physics, 64);
    const auto freed = allocator.getStats(star::MemTag::Physics);
    STAR_CHECK(freed && freed->live_bytes == 0 && freed->peak_bytes == 32 && freed->live_allocations == 0);
    const auto other = allocator.getStats(star::MemTag::Renderer);
    STAR_CHECK(other && other->live_bytes == 24);
    allocator.deallocate(*renderer, 24, star::MemTag::Renderer);

    const auto invalid_tag = allocator.allocate(1, star::MemTag::Count);
    STAR_CHECK(!invalid_tag && invalid_tag.error() == star::AllocationError::InvalidTag);
    const auto invalid_alignment = allocator.allocate(1, star::MemTag::Core, 3);
    STAR_CHECK(!invalid_alignment && invalid_alignment.error() == star::AllocationError::InvalidAlignment);
}

void *rejectAllocation(void *, std::size_t, std::size_t) noexcept { return nullptr; }

void ignoreDeallocation(void *, void *, std::size_t, std::size_t) noexcept {}

STAR_TEST(trackedAllocatorSkipsFailedAllocationsAndSupportsConcurrentUse) {
    star::TrackedAllocator failing(nullptr, rejectAllocation, ignoreDeallocation);
    const auto error = failing.allocate(1, star::MemTag::Core);
    STAR_CHECK(!error && error.error() == star::AllocationError::Exhausted);
    const auto failed_stats = failing.getStats(star::MemTag::Core);
    STAR_CHECK(failed_stats && failed_stats->peak_bytes == 0 && failed_stats->live_allocations == 0);

    star::TrackedAllocator shared;
    std::array<bool, 4> succeeded{};
    {
        std::array<std::jthread, 4> threads;
        for (std::size_t index = 0; index < threads.size(); ++index) {
            threads[index] = std::jthread([&, index] {
                for (int iteration = 0; iteration < 100; ++iteration) {
                    const auto allocated = shared.allocate(16, star::MemTag::Core);
                    if (!allocated) {
                        return;
                    }
                    shared.deallocate(*allocated, 16, star::MemTag::Core);
                }
                succeeded[index] = true;
            });
        }
    }
    for (const bool success : succeeded) {
        STAR_CHECK(success);
    }
    const auto active = shared.getStats(star::MemTag::Core);
    STAR_CHECK(active && active->live_allocations == 0 && active->live_bytes == 0 && active->peak_bytes >= 16);
}

} // namespace
