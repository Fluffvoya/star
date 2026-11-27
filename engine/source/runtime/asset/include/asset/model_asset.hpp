#pragma once

#include "asset/export.hpp"
#include "asset/texture_asset.hpp"
#include "core/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace star {

struct StaticMeshVertex {
    math::Vector3 position;
    math::Vector3 normal;
    math::Vector2 uv;
};

struct StaticPbrMaterial {
    std::array<float, 4> base_color{1, 1, 1, 1};
    float metallic = 1.0F;
    float roughness = 1.0F;
    std::optional<std::size_t> base_color_texture;
    std::optional<std::size_t> metallic_roughness_texture;
};

struct StaticMeshPart {
    std::string name;
    std::string material_name;
    std::optional<StaticPbrMaterial> material;
    std::vector<StaticMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct StaticModelAsset {
    std::vector<StaticMeshPart> parts;
    std::vector<TextureAsset> textures;
};

enum class ModelAssetError : std::uint8_t { InvalidHeader, UnsupportedVersion, InvalidModel };

[[nodiscard]] STAR_ASSET_API std::expected<std::vector<std::byte>, ModelAssetError>
writeModelAsset(const StaticModelAsset &model);

[[nodiscard]] STAR_ASSET_API std::expected<StaticModelAsset, ModelAssetError>
readModelAsset(std::span<const std::byte> bytes);

} // namespace star
