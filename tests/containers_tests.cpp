#include "test_support.hpp"

#include "core/containers.hpp"

#include <array>
#include <string>

namespace {

STAR_TEST(flatMapMaintainsSortedUniqueKeys) {
    star::FlatMap<int, std::string> map;
    STAR_CHECK(map.insertOrAssign(4, "four"));
    STAR_CHECK(map.insertOrAssign(1, "one"));
    STAR_CHECK(map.insertOrAssign(3, "three"));
    STAR_CHECK(!map.insertOrAssign(3, "updated"));
    STAR_CHECK(map.size() == 3 && map.find(3) != nullptr && *map.find(3) == "updated");

    std::array<int, 3> order{};
    std::size_t index = 0;
    for (const auto &[key, value] : map) {
        static_cast<void>(value);
        order[index++] = key;
    }
    const std::array expected_order{1, 3, 4};
    STAR_CHECK(order == expected_order);
    STAR_CHECK(map.erase(1) && !map.erase(1) && !map.contains(1));
    map.clear();
    STAR_CHECK(map.empty());
}

STAR_TEST(flatSetMaintainsSortedMembership) {
    star::FlatSet<int> set;
    STAR_CHECK(set.insert(5) && set.insert(2) && !set.insert(5));
    STAR_CHECK(set.contains(2) && set.contains(5) && !set.contains(3));
    STAR_CHECK(set.erase(2) && !set.erase(2) && !set.contains(2));
    STAR_CHECK(set.size() == 1);
}

STAR_TEST(hashMapUsesStandardHashSemantics) {
    star::HashMap<std::string, int> map;
    map.emplace("one", 1);
    map["two"] = 2;
    STAR_CHECK(map.size() == 2 && map.at("one") == 1 && map.at("two") == 2);
}

STAR_TEST(ringBufferPreservesFifoOrderAndRejectsOverflow) {
    star::RingBuffer<int> buffer(3);
    STAR_CHECK(buffer.capacity() == 3 && buffer.empty());
    STAR_CHECK(buffer.tryPush(1) && buffer.tryPush(2) && buffer.tryPush(3));
    STAR_CHECK(!buffer.tryPush(4) && buffer.full() && *buffer.front() == 1 && *buffer.back() == 3);
    STAR_CHECK(buffer.pop() == 1 && buffer.tryPush(4));
    STAR_CHECK(buffer.pop() == 2 && buffer.pop() == 3 && buffer.pop() == 4);
    STAR_CHECK(!buffer.pop() && buffer.empty());
    buffer.clear();
    STAR_CHECK(buffer.empty());
}

} // namespace
