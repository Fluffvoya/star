#pragma once

#include "asset-pipeline/export.hpp"
#include "core/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace star::asset_pipeline {

enum class StaticModelFormat : std::uint8_t { Fbx, Obj };

struct StaticMeshVertex {
    math::Vector3 position;
    math::Vector3 normal;
    math::Vector2 uv;
};

struct ImportedPbrMaterial {
    std::array<float, 4> base_color{1, 1, 1, 1};
    float metallic = 1.0F;
    float roughness = 1.0F;
};

struct StaticMeshPart {
    std::string name;
    std::string material_name;
    std::optional<ImportedPbrMaterial> material;
    std::vector<StaticMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct ImportedStaticModel {
    std::vector<StaticMeshPart> parts;
};

struct ModelImportError {
    std::string message;
};

// Bakes instance transforms into right-handed Y-up meters and produces top-left-origin UVs.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<ImportedStaticModel, ModelImportError>
importStaticModel(std::span<const std::byte> source, StaticModelFormat format);

using GltfBufferResolver =
    std::function<std::expected<std::vector<std::byte>, ModelImportError>(std::string_view relative_path)>;

// The resolver supplies a glTF document's external buffer contents. The importer does not open files.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<ImportedStaticModel, ModelImportError>
importGltfStaticModel(std::span<const std::byte> source, const GltfBufferResolver &resolve_buffer = {});

} // namespace star::asset_pipeline
