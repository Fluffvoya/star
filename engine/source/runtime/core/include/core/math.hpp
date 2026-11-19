#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace star::math {

struct Vector2 {
    float x = 0.0F;
    float y = 0.0F;

    friend Vector2 operator+(Vector2 left, Vector2 right) { return {left.x + right.x, left.y + right.y}; }
    friend Vector2 operator-(Vector2 left, Vector2 right) { return {left.x - right.x, left.y - right.y}; }
    friend Vector2 operator*(Vector2 value, float scalar) { return {value.x * scalar, value.y * scalar}; }
    friend Vector2 operator*(float scalar, Vector2 value) { return value * scalar; }
    friend Vector2 operator/(Vector2 value, float scalar) { return {value.x / scalar, value.y / scalar}; }
    friend bool operator==(Vector2, Vector2) = default;
};

struct Vector3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend Vector3 operator+(Vector3 left, Vector3 right) {
        return {left.x + right.x, left.y + right.y, left.z + right.z};
    }
    friend Vector3 operator-(Vector3 left, Vector3 right) {
        return {left.x - right.x, left.y - right.y, left.z - right.z};
    }
    friend Vector3 operator-(Vector3 value) { return {-value.x, -value.y, -value.z}; }
    friend Vector3 operator*(Vector3 value, float scalar) {
        return {value.x * scalar, value.y * scalar, value.z * scalar};
    }
    friend Vector3 operator*(float scalar, Vector3 value) { return value * scalar; }
    friend Vector3 operator/(Vector3 value, float scalar) {
        return {value.x / scalar, value.y / scalar, value.z / scalar};
    }
    friend bool operator==(Vector3, Vector3) = default;
};

inline float dot(Vector2 left, Vector2 right) { return left.x * right.x + left.y * right.y; }
inline float dot(Vector3 left, Vector3 right) { return left.x * right.x + left.y * right.y + left.z * right.z; }
inline float cross(Vector2 left, Vector2 right) { return left.x * right.y - left.y * right.x; }
inline Vector3 cross(Vector3 left, Vector3 right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}
inline float lengthSquared(Vector2 value) { return dot(value, value); }
inline float lengthSquared(Vector3 value) { return dot(value, value); }
inline float length(Vector2 value) { return std::sqrt(lengthSquared(value)); }
inline float length(Vector3 value) { return std::sqrt(lengthSquared(value)); }
inline Vector2 normalize(Vector2 value) {
    const float magnitude = length(value);
    return magnitude == 0.0F ? Vector2{} : value / magnitude;
}
inline Vector3 normalize(Vector3 value) {
    const float magnitude = length(value);
    return magnitude == 0.0F ? Vector3{} : value / magnitude;
}

struct Quaternion {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    [[nodiscard]] static Quaternion identity() { return {}; }
    [[nodiscard]] static Quaternion fromAxisAngle(Vector3 axis, float radians) {
        const Vector3 unit_axis = normalize(axis);
        const float half_angle = radians * 0.5F;
        const float sine = std::sin(half_angle);
        return {unit_axis.x * sine, unit_axis.y * sine, unit_axis.z * sine, std::cos(half_angle)};
    }

    friend Quaternion operator*(Quaternion left, Quaternion right) {
        return {left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
                left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
                left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
                left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
    }

    [[nodiscard]] Quaternion conjugate() const { return {-x, -y, -z, w}; }
    [[nodiscard]] Vector3 rotate(Vector3 value) const {
        const Quaternion vector_quaternion{value.x, value.y, value.z, 0.0F};
        const Quaternion rotated = *this * vector_quaternion * conjugate();
        return {rotated.x, rotated.y, rotated.z};
    }
};

struct Matrix4 {
    struct Vector4 {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        float w = 0.0F;
    };

    std::array<Vector4, 4> columns{};

    [[nodiscard]] static Matrix4 identity() {
        Matrix4 matrix;
        matrix.columns = {
            {{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}}};
        return matrix;
    }

    [[nodiscard]] static Matrix4 translation(Vector3 value) {
        Matrix4 matrix = identity();
        matrix.columns[3] = {value.x, value.y, value.z, 1.0F};
        return matrix;
    }

    [[nodiscard]] static Matrix4 scale(Vector3 value) {
        Matrix4 matrix = identity();
        matrix.columns[0].x = value.x;
        matrix.columns[1].y = value.y;
        matrix.columns[2].z = value.z;
        return matrix;
    }

    [[nodiscard]] Vector4 transform(Vector4 value) const {
        return {columns[0].x * value.x + columns[1].x * value.y + columns[2].x * value.z + columns[3].x * value.w,
                columns[0].y * value.x + columns[1].y * value.y + columns[2].y * value.z + columns[3].y * value.w,
                columns[0].z * value.x + columns[1].z * value.y + columns[2].z * value.z + columns[3].z * value.w,
                columns[0].w * value.x + columns[1].w * value.y + columns[2].w * value.z + columns[3].w * value.w};
    }

    [[nodiscard]] Vector3 transformPoint(Vector3 value) const {
        const Vector4 transformed = transform({value.x, value.y, value.z, 1.0F});
        return {transformed.x, transformed.y, transformed.z};
    }
};

using Vec2 = Vector2;
using Vec3 = Vector3;
using Quat = Quaternion;
using Mat4 = Matrix4;

inline constexpr float pi = std::numbers::pi_v<float>;

} // namespace star::math
