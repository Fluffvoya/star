#pragma once

#include "asset/export.hpp"
#include "core/guid.hpp"
#include "serialization/archive.hpp"

#include <compare>
#include <cstdint>
#include <expected>

namespace star {

enum class MaterialBlendMode : std::uint8_t { Opaque, Masked, Transparent };
enum class MaterialShadingModel : std::uint8_t { Lit, Unlit };

struct MaterialRenderState {
    MaterialBlendMode blend = MaterialBlendMode::Opaque;
    MaterialShadingModel shading = MaterialShadingModel::Lit;
    bool double_sided = false;
    bool depth_test = true;

    friend auto operator<=>(const MaterialRenderState &, const MaterialRenderState &) = default;
};

struct MaterialVariantKey {
    Guid shader;
    MaterialRenderState state;

    friend auto operator<=>(const MaterialVariantKey &, const MaterialVariantKey &) = default;
};

struct MaterialAsset {
    Guid shader;
    MaterialRenderState state;
    serialization::ArchiveValue::Mapping parameters;
    serialization::ArchiveValue::Mapping extensions;
    serialization::ArchiveValue::Mapping state_extensions;

    [[nodiscard]] MaterialVariantKey getVariantKey() const noexcept { return {shader, state}; }
    friend bool operator==(const MaterialAsset &, const MaterialAsset &) = default;
};

struct MaterialInstanceAsset {
    Guid parent;
    serialization::ArchiveValue::Mapping overrides;
    serialization::ArchiveValue::Mapping extensions;

    friend bool operator==(const MaterialInstanceAsset &, const MaterialInstanceAsset &) = default;
};

// Source YAML and derived binary archives use the same versioned fields.
[[nodiscard]] STAR_ASSET_API std::expected<MaterialAsset, serialization::ArchiveError>
readMaterial(const serialization::ArchiveDocument &document,
             serialization::LoadPolicy policy = serialization::LoadPolicy::Preserve);

[[nodiscard]] STAR_ASSET_API std::expected<MaterialInstanceAsset, serialization::ArchiveError>
readMaterialInstance(const serialization::ArchiveDocument &document,
                     serialization::LoadPolicy policy = serialization::LoadPolicy::Preserve);

[[nodiscard]] STAR_ASSET_API std::expected<serialization::ArchiveDocument, serialization::ArchiveError>
writeMaterial(const MaterialAsset &material);

[[nodiscard]] STAR_ASSET_API std::expected<serialization::ArchiveDocument, serialization::ArchiveError>
writeMaterialInstance(const MaterialInstanceAsset &instance);

[[nodiscard]] STAR_ASSET_API std::expected<MaterialAsset, serialization::ArchiveError>
resolveMaterialInstance(Guid parent_guid, const MaterialAsset &parent, const MaterialInstanceAsset &instance);

} // namespace star
