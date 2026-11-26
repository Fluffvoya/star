#include "transform_math.hpp"

#include <cmath>

namespace star::scene_detail {
namespace {

using math::Mat4;
using math::Quat;
using math::Vec3;

Vec3 xyz(Mat4::Vector4 value) noexcept { return {value.x, value.y, value.z}; }

Mat4 affine(Vec3 x, Vec3 y, Vec3 z, Vec3 position) noexcept {
    return {{{{x.x, x.y, x.z, 0.0F},
              {y.x, y.y, y.z, 0.0F},
              {z.x, z.y, z.z, 0.0F},
              {position.x, position.y, position.z, 1.0F}}}};
}

std::optional<Mat4> invert(const Mat4 &matrix) noexcept {
    const Vec3 x = xyz(matrix.columns[0]);
    const Vec3 y = xyz(matrix.columns[1]);
    const Vec3 z = xyz(matrix.columns[2]);
    const Vec3 cross_yz = math::cross(y, z);
    const float determinant = math::dot(x, cross_yz);
    if (determinant == 0.0F || !std::isfinite(determinant)) {
        return std::nullopt;
    }
    const Vec3 row_x = cross_yz / determinant;
    const Vec3 row_y = math::cross(z, x) / determinant;
    const Vec3 row_z = math::cross(x, y) / determinant;
    const Vec3 position = -xyz(matrix.columns[3]);
    return affine({row_x.x, row_y.x, row_z.x}, {row_x.y, row_y.y, row_z.y}, {row_x.z, row_y.z, row_z.z},
                  {math::dot(row_x, position), math::dot(row_y, position), math::dot(row_z, position)});
}

Quat rotationFromBasis(Vec3 x, Vec3 y, Vec3 z) noexcept {
    const float trace = x.x + y.y + z.z;
    if (trace > 0.0F) {
        const float s = 2.0F * std::sqrt(1.0F + trace);
        return {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, s / 4.0F};
    }
    if (x.x >= y.y && x.x >= z.z) {
        const float s = 2.0F * std::sqrt(1.0F + x.x - y.y - z.z);
        return {s / 4.0F, (x.y + y.x) / s, (x.z + z.x) / s, (y.z - z.y) / s};
    }
    if (y.y >= z.z) {
        const float s = 2.0F * std::sqrt(1.0F + y.y - x.x - z.z);
        return {(x.y + y.x) / s, s / 4.0F, (y.z + z.y) / s, (z.x - x.z) / s};
    }
    const float s = 2.0F * std::sqrt(1.0F + z.z - x.x - y.y);
    return {(x.z + z.x) / s, (y.z + z.y) / s, s / 4.0F, (x.y - y.x) / s};
}

std::optional<Transform> decompose(const Mat4 &matrix) noexcept {
    constexpr float tolerance = 1e-4F;
    const Vec3 position = xyz(matrix.columns[3]);
    Vec3 x = xyz(matrix.columns[0]);
    Vec3 y = xyz(matrix.columns[1]);
    Vec3 z = xyz(matrix.columns[2]);
    Vec3 scale{math::length(x), math::length(y), math::length(z)};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) || scale.x == 0.0F ||
        scale.y == 0.0F || scale.z == 0.0F) {
        return std::nullopt;
    }
    x = x / scale.x;
    y = y / scale.y;
    z = z / scale.z;
    if (std::abs(math::dot(x, y)) > tolerance || std::abs(math::dot(x, z)) > tolerance ||
        std::abs(math::dot(y, z)) > tolerance) {
        return std::nullopt;
    }
    if (math::dot(math::cross(x, y), z) < 0.0F) {
        scale.x = -scale.x;
        x = -x;
    }
    return Transform{position, rotationFromBasis(x, y, z), scale};
}

} // namespace

math::Mat4 localMatrix(const Transform &transform) noexcept {
    const auto &q = transform.rotation;
    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    const auto rotation = norm == 0.0F ? Quat{} : Quat{q.x / norm, q.y / norm, q.z / norm, q.w / norm};
    return affine(rotation.rotate({transform.scale.x, 0.0F, 0.0F}), rotation.rotate({0.0F, transform.scale.y, 0.0F}),
                  rotation.rotate({0.0F, 0.0F, transform.scale.z}), transform.position);
}

math::Mat4 multiply(const math::Mat4 &left, const math::Mat4 &right) noexcept {
    return {{left.transform(right.columns[0]), left.transform(right.columns[1]), left.transform(right.columns[2]),
             left.transform(right.columns[3])}};
}

std::optional<Transform> relativeTransform(const math::Mat4 &parent, const math::Mat4 &world) noexcept {
    const auto inverse = invert(parent);
    return inverse ? decompose(multiply(*inverse, world)) : std::nullopt;
}

} // namespace star::scene_detail
