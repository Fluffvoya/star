#pragma once

#include "asset-pipeline/export.hpp"
#include "asset/model_asset.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace star::asset_pipeline {

enum class StaticModelFormat : std::uint8_t { Fbx, Obj };

using star::StaticMeshPart;
using star::StaticMeshVertex;
using ImportedPbrMaterial = StaticPbrMaterial;
using ImportedStaticModel = StaticModelAsset;

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

// The resolver supplies external buffer and image contents for this entry point.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<ImportedStaticModel, ModelImportError>
importGltfTexturedModel(std::span<const std::byte> source, const GltfBufferResolver &resolve_resource = {});

} // namespace star::asset_pipeline
