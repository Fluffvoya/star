#pragma once

#include "scene/components.hpp"

#include <optional>

namespace star::scene_detail {

math::Mat4 localMatrix(const Transform &transform) noexcept;
math::Mat4 multiply(const math::Mat4 &left, const math::Mat4 &right) noexcept;
std::optional<Transform> relativeTransform(const math::Mat4 &parent, const math::Mat4 &world) noexcept;

} // namespace star::scene_detail
