#pragma once

#include "asset-pipeline/derived_data_cache.hpp"
#include "asset-pipeline/export.hpp"
#include "asset-pipeline/texture_importer.hpp"

#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace star::asset_pipeline {

enum class CachedTextureErrorCode { InvalidMeta, CacheIo, ImportFailed };

struct CachedTextureError {
    CachedTextureErrorCode code;
    std::string detail;
};

struct CachedTexture {
    ImportedTexture texture;
    DerivedDataKey key;
    bool cache_hit;
};

// Uses the TextureImporter `.meta` Settings.Usage and Settings.GenerateMips fields.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<CachedTexture, CachedTextureError>
importCachedTexture(std::span<const std::byte> source, const AssetMeta &meta, std::string_view platform,
                    const DerivedDataCache &cache);

} // namespace star::asset_pipeline
