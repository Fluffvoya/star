#pragma once

#include "Core/Export.hpp"

#include <compare>
#include <cstdint>
#include <string_view>

namespace star {

class STAR_CORE_API Name final {
  public:
    constexpr Name() = default;
    explicit Name(std::string_view text);

    [[nodiscard]] constexpr std::uint32_t GetId() const { return m_id; }

    [[nodiscard]] constexpr bool IsEmpty() const { return m_id == 0; }

    [[nodiscard]] std::string_view GetString() const;

    friend constexpr bool operator==(const Name &, const Name &) = default;
    friend constexpr auto operator<=>(const Name &, const Name &) = default;

  private:
    explicit constexpr Name(std::uint32_t id) : m_id(id) {}

    std::uint32_t m_id = 0;
};

} // namespace star
