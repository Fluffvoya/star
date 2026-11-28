#pragma once

#include "asset/model_asset.hpp"
#include "core/math.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace star::renderer {

// Upper bound for uploaded point lights. Clusters still hold at most STAR_CLUSTER_MAX_LIGHTS.
inline constexpr std::size_t kMaxRenderPointLights = 64;

// A pinhole camera whose transform follows the engine convention: -Z is forward.
struct RenderCamera {
    math::Mat4 world = math::Mat4::identity();
    float vertical_fov = math::pi / 3.0F;
    float near_plane = 0.1F;
    float far_plane = 1000.0F;
};

// Direction points from the surface toward the light, matching the shading term.
struct RenderDirectionalLight {
    math::Vec3 direction{-0.45F, 0.85F, 0.35F};
    math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 3.5F;
};

struct RenderPointLight {
    math::Vec3 position{};
    math::Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
    float range = 10.0F;
};

// Bounds of the baked geometry. The renderer uses them for shadow cascades and the fallback camera.
struct RenderBounds {
    math::Vec3 center{};
    float radius = 0.0F;
};

// Builds a camera transform from a view direction. Falls back to a world-axis up vector when the
// requested up vector is parallel to the view direction.
[[nodiscard]] inline RenderCamera makeLookAtCamera(math::Vec3 eye, math::Vec3 target, math::Vec3 up,
                                                   float vertical_fov = math::pi / 3.0F, float near_plane = 0.1F,
                                                   float far_plane = 1000.0F) {
    using namespace math;
    const auto forward = normalize(target - eye);
    auto right = cross(forward, up);
    if (lengthSquared(right) <= 1e-12F) {
        right = cross(forward, {1.0F, 0.0F, 0.0F});
    }
    if (lengthSquared(right) <= 1e-12F) {
        right = cross(forward, {0.0F, 0.0F, 1.0F});
    }
    right = normalize(right);
    const auto camera_up = cross(right, forward);
    Mat4 world;
    world.columns[0] = {right.x, right.y, right.z, 0.0F};
    world.columns[1] = {camera_up.x, camera_up.y, camera_up.z, 0.0F};
    world.columns[2] = {-forward.x, -forward.y, -forward.z, 0.0F};
    world.columns[3] = {eye.x, eye.y, eye.z, 1.0F};
    return {world, vertical_fov, near_plane, far_plane};
}

// A renderer-owned snapshot of drawable scene data. Scene extraction fills it; the renderer never
// reads gameplay storage. Geometry is already baked into world space.
struct RenderScene {
    StaticModelAsset model;
    std::vector<std::uint8_t> shadow_casters; // One entry per model part.
    std::vector<RenderCamera> cameras;
    std::optional<RenderDirectionalLight> directional_light;
    std::vector<RenderPointLight> point_lights;
    RenderBounds bounds;
};

} // namespace star::renderer
