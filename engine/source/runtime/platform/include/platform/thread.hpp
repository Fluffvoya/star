#pragma once

#include "platform/export.hpp"

#include <expected>
#include <string_view>
#include <system_error>

namespace star {

// Sets the native name of the calling thread. Pass a nonempty UTF-8 name without null bytes.
[[nodiscard]] STAR_PLATFORM_API std::expected<void, std::error_code>
setCurrentThreadName(std::string_view name) noexcept;

} // namespace star
