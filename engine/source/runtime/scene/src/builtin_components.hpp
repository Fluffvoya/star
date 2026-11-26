#pragma once

#include "reflection/reflection.hpp"

#include <span>

namespace star::scene_detail {

std::span<const reflect::TypeInfo> builtinComponents() noexcept;

} // namespace star::scene_detail
