#pragma once

#include "core/guid.hpp"
#include "core/math.hpp"
#include "scene/game_object.hpp"

#include <cstdint>
#include <vector>

namespace star {

// A specialization supplies the same qualified name as the registered metadata.
template <class Component> struct ComponentTraits;

struct Transform {
    math::Vec3 position;
    math::Quat rotation;
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct WorldTransform {
    math::Mat4 matrix = math::Mat4::identity();
};

struct Hierarchy {
    ObjectHandle parent;
    ObjectHandle first_child;
    ObjectHandle last_child;
    ObjectHandle previous_sibling;
    ObjectHandle next_sibling;
    std::uint32_t child_count = 0;
    std::uint32_t depth = 0;
    std::uint32_t sibling_index = 0;
};

struct Camera {
    float vertical_fov = math::pi / 3.0F;
    float near_plane = 0.1F;
    float far_plane = 1000.0F;
};

struct MeshRenderer {
    Guid mesh;
    std::vector<Guid> materials;
    bool cast_shadows = true;
    std::uint32_t layer_mask = 1;
};

struct Light {
    math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 10.0F;
};

template <> struct ComponentTraits<Transform> {
    static constexpr auto name = "star::Transform";
};
template <> struct ComponentTraits<Camera> {
    static constexpr auto name = "star::Camera";
};
template <> struct ComponentTraits<MeshRenderer> {
    static constexpr auto name = "star::MeshRenderer";
};
template <> struct ComponentTraits<Light> {
    static constexpr auto name = "star::Light";
};

} // namespace star
