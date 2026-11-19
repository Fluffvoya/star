#pragma once

#include "Core/Export.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace star {

enum class GuidParseError : std::uint8_t {
    InvalidLength,
    InvalidCharacter,
};

class STAR_CORE_API Guid final {
  public:
    constexpr Guid() = default;

    [[nodiscard]] static Guid Generate();
    [[nodiscard]] static std::expected<Guid, GuidParseError> Parse(std::string_view text);

    [[nodiscard]] constexpr bool IsValid() const {
        for (const std::uint8_t byte : m_bytes) {
            if (byte != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr const std::array<std::uint8_t, 16> &GetBytes() const { return m_bytes; }

    [[nodiscard]] std::string ToString() const;

    friend constexpr bool operator==(const Guid &, const Guid &) = default;
    friend constexpr auto operator<=>(const Guid &, const Guid &) = default;

  private:
    explicit constexpr Guid(std::array<std::uint8_t, 16> bytes) : m_bytes(bytes) {}

    std::array<std::uint8_t, 16> m_bytes{};
};

} // namespace star
