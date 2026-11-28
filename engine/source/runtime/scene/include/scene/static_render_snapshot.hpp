#pragma once

#include "asset/model_asset.hpp"
#include "scene/export.hpp"
#include "scene/game_object.hpp"

#include <cstdint>
#include <expected>
#include <vector>

namespace star {

class AssetManager;
class Scene;

enum class StaticRenderErrorCode : std::uint8_t { QueryFailed, AssetLoadFailed, InvalidTransform, AllocationFailed };

struct StaticRenderError {
    StaticRenderErrorCode code;
    ObjectHandle object{};
    Guid mesh{};
};

struct StaticRenderSnapshot {
    StaticModelAsset model;
    std::vector<std::uint8_t> shadow_casters;
};

// Call after Scene transform propagation. The snapshot owns its transformed geometry and textures.
[[nodiscard]] STAR_SCENE_API std::expected<StaticRenderSnapshot, StaticRenderError>
extractStaticRenderSnapshot(Scene &scene, AssetManager &assets, std::uint32_t layer_mask = ~std::uint32_t{0});

} // namespace star
