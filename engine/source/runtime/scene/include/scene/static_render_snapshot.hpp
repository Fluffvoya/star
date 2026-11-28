#pragma once

#include "asset/asset_manager.hpp"
#include "renderer/render_scene.hpp"
#include "scene/export.hpp"
#include "scene/game_object.hpp"

#include <cstdint>
#include <expected>

namespace star {

class Scene;

enum class StaticRenderErrorCode : std::uint8_t {
    QueryFailed,
    AssetLoadFailed,
    InvalidTransform,
    InvalidCamera,
    InvalidLight,
    AllocationFailed,
};

struct StaticRenderError {
    StaticRenderErrorCode code;
    ObjectHandle object{};
    Guid mesh{};
};

using StaticRenderSnapshot = renderer::RenderScene;

// Call after Scene transform propagation. The snapshot owns its transformed geometry and textures.
// It carries every scene camera, the first directional light and up to kMaxRenderPointLights point lights.
[[nodiscard]] STAR_SCENE_API std::expected<StaticRenderSnapshot, StaticRenderError>
extractStaticRenderSnapshot(Scene &scene, AssetManager &assets, std::uint32_t layer_mask = ~std::uint32_t{0});

} // namespace star
