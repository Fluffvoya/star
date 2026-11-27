#pragma once

#include "asset-pipeline/derived_data_cache.hpp"
#include "asset-pipeline/export.hpp"
#include "asset-pipeline/model_importer.hpp"

#include <expected>
#include <span>
#include <string_view>

namespace star::asset_pipeline {

struct CachedModel {
    ImportedStaticModel model;
    DerivedDataKey key;
    bool cache_hit;
};

// External glTF inputs require a resolver and fingerprints in a stable order.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<CachedModel, ModelImportError>
importCachedModel(std::span<const std::byte> source, const AssetMeta &meta, std::string_view platform,
                  const DerivedDataCache &cache, const GltfBufferResolver &resolve = {},
                  std::span<const DerivedDataKey> fingerprints = {});

} // namespace star::asset_pipeline
