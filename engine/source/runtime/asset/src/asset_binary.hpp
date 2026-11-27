#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace star::asset_binary {

inline void appendUInt32(std::vector<std::byte> &output, std::uint32_t field) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(std::byte((field >> shift) & 0xffU));
    }
}

inline std::optional<std::uint32_t> takeUInt32(std::span<const std::byte> &bytes) {
    if (bytes.size() < 4) {
        return std::nullopt;
    }
    std::uint32_t field = 0;
    for (unsigned index = 0; index < 4; ++index) {
        field |= std::to_integer<std::uint32_t>(bytes[index]) << (8 * index);
    }
    bytes = bytes.subspan(4);
    return field;
}

} // namespace star::asset_binary
