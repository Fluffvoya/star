#pragma once

#include "platform/export.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>

namespace star {

// Sets the native name of the calling thread. Pass a nonempty UTF-8 name without null bytes.
[[nodiscard]] STAR_PLATFORM_API std::expected<void, std::error_code>
setCurrentThreadName(std::string_view name) noexcept;

// Restricts the calling thread to the listed logical CPU indices where the platform exposes this operation.
[[nodiscard]] STAR_PLATFORM_API std::expected<void, std::error_code>
setCurrentThreadAffinity(std::span<const std::uint32_t> cpu_indices) noexcept;

} // namespace star
