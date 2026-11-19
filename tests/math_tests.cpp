#include "test_support.hpp"

#include "core/math.hpp"

#include <cmath>

namespace {

bool near(float left, float right, float epsilon = 0.0001F) { return std::fabs(left - right) < epsilon; }

STAR_TEST(mathVectorsProvideDotCrossAndNormalization) {
    const star::math::Vec3 x{1.0F, 0.0F, 0.0F};
    const star::math::Vec3 y{0.0F, 1.0F, 0.0F};
    const auto z = star::math::cross(x, y);
    const star::math::Vec3 expected_z{0.0F, 0.0F, 1.0F};
    STAR_CHECK(z == expected_z);
    STAR_CHECK(star::math::dot(x, y) == 0.0F);
    const auto normalized = star::math::normalize(star::math::Vec3{3.0F, 4.0F, 0.0F});
    STAR_CHECK(near(star::math::length(normalized), 1.0F));
    STAR_CHECK(star::math::normalize(star::math::Vec3{}) == star::math::Vec3{});
}

STAR_TEST(mathQuaternionUsesRightHandedAxisRotation) {
    const auto rotation = star::math::Quat::fromAxisAngle({0.0F, 1.0F, 0.0F}, star::math::pi * 0.5F);
    const auto rotated = rotation.rotate({0.0F, 0.0F, -1.0F});
    STAR_CHECK(near(rotated.x, -1.0F) && near(rotated.y, 0.0F) && near(rotated.z, 0.0F));
}

STAR_TEST(mathMatrixUsesColumnMajorColumnVectors) {
    const auto matrix = star::math::Mat4::translation({2.0F, 3.0F, 4.0F});
    const auto transformed = matrix.transformPoint({1.0F, 2.0F, 3.0F});
    const star::math::Vec3 expected_translation{3.0F, 5.0F, 7.0F};
    STAR_CHECK(transformed == expected_translation);

    const auto scaled = star::math::Mat4::scale({2.0F, 3.0F, 4.0F});
    const star::math::Vec3 expected_scale{2.0F, 3.0F, 4.0F};
    STAR_CHECK(scaled.transformPoint({1.0F, 1.0F, 1.0F}) == expected_scale);
}

} // namespace
