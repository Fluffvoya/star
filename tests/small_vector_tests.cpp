#include "test_support.hpp"

#include "core/containers.hpp"

#include <string>

namespace {

STAR_TEST(smallVectorUsesInlineStorageBeforeGrowing) {
    star::SmallVector<int, 2> values;
    STAR_CHECK(values.empty() && values.capacity() == 2);
    values.pushBack(1);
    values.emplaceBack(2);
    const int *inline_data = values.data();
    STAR_CHECK(values.size() == 2 && values[0] == 1 && values[1] == 2);
    values.pushBack(3);
    STAR_CHECK(values.size() == 3 && values.capacity() >= 3 && values.data() != inline_data);
    STAR_CHECK(values.at(2) == 3);
}

STAR_TEST(smallVectorMovesCopiesAndClearsObjects) {
    star::SmallVector<std::string, 2> original;
    original.emplaceBack("one");
    original.emplaceBack("two");
    original.emplaceBack("three");
    const auto copied = original;
    STAR_CHECK(copied.size() == 3 && copied[1] == "two");

    auto moved = std::move(original);
    STAR_CHECK(original.empty() && moved.size() == 3 && moved[2] == "three");
    moved.popBack();
    moved.clear();
    STAR_CHECK(moved.empty());
}

STAR_TEST(smallVectorSupportsZeroInlineCapacityAndBoundsChecks) {
    star::SmallVector<int, 0> values;
    STAR_CHECK(values.capacity() == 0);
    values.pushBack(7);
    STAR_CHECK(values.size() == 1 && values[0] == 7 && values.capacity() >= 1);

    bool threw = false;
    try {
        static_cast<void>(values.at(1));
    } catch (const std::out_of_range &) {
        threw = true;
    }
    STAR_CHECK(threw);
}

} // namespace
