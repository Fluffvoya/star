#pragma once

#include <cstdint>

namespace star {

enum class AllocationError : std::uint8_t {
    InvalidSize,
    InvalidAlignment,
    InvalidTag,
    Exhausted,
    ConstructionFailed,
};

} // namespace star
