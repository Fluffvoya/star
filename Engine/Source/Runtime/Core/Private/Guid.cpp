#include "Core/Guid.hpp"

#include <array>
#include <format>
#include <limits>
#include <random>

namespace star {
namespace {

constexpr std::size_t kGuidTextLength = 32;
constexpr std::size_t kGuidByteCount = 16;

[[nodiscard]] constexpr std::uint8_t HexValue(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
        return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    return std::numeric_limits<std::uint8_t>::max();
}

[[nodiscard]] std::mt19937_64 &RandomGenerator() {
    thread_local std::mt19937_64 generator = [] {
        std::random_device randomDevice;
        std::array<std::uint32_t, 8> seedData{};
        for (std::uint32_t &value : seedData) {
            value = randomDevice();
        }
        std::seed_seq seed(seedData.begin(), seedData.end());
        return std::mt19937_64(seed);
    }();
    return generator;
}

} // namespace

Guid Guid::Generate() {
    std::array<std::uint8_t, kGuidByteCount> bytes{};
    std::uniform_int_distribution<std::uint32_t> distribution(0, std::numeric_limits<std::uint8_t>::max());

    auto &generator = RandomGenerator();
    for (std::uint8_t &byte : bytes) {
        byte = static_cast<std::uint8_t>(distribution(generator));
    }

    // RFC 4122 variant and version bits.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return Guid(bytes);
}

std::expected<Guid, GuidParseError> Guid::Parse(std::string_view text) {
    if (text.size() != kGuidTextLength) {
        return std::unexpected(GuidParseError::InvalidLength);
    }

    std::array<std::uint8_t, kGuidByteCount> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const std::uint8_t high = HexValue(text[index * 2]);
        const std::uint8_t low = HexValue(text[index * 2 + 1]);
        if (high == std::numeric_limits<std::uint8_t>::max() || low == std::numeric_limits<std::uint8_t>::max()) {
            return std::unexpected(GuidParseError::InvalidCharacter);
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4U) | low);
    }

    return Guid(bytes);
}

std::string Guid::ToString() const {
    std::string result;
    result.reserve(kGuidTextLength);
    for (const std::uint8_t byte : m_bytes) {
        result += std::format("{:02x}", byte);
    }
    return result;
}

} // namespace star
