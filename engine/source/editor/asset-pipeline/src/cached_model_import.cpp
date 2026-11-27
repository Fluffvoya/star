#include "asset-pipeline/cached_model_import.hpp"

#include <utility>

namespace star::asset_pipeline {
namespace {

std::unexpected<ModelImportError> importFailed(std::string message) {
    return std::unexpected(ModelImportError{std::move(message)});
}

std::expected<ImportedStaticModel, ModelImportError>
importModel(std::span<const std::byte> source, std::string_view importer, const GltfBufferResolver &resolve) {
    if (importer == "star::GltfImporter") {
        return importGltfTexturedModel(source, resolve);
    }
    if (importer == "star::FbxImporter") {
        return importStaticModel(source, StaticModelFormat::Fbx);
    }
    if (importer == "star::ObjImporter") {
        return importStaticModel(source, StaticModelFormat::Obj);
    }
    return importFailed("Unknown static model importer");
}

} // namespace

std::expected<CachedModel, ModelImportError> importCachedModel(std::span<const std::byte> source, const AssetMeta &meta,
                                                               std::string_view platform, const DerivedDataCache &cache,
                                                               const GltfBufferResolver &resolve,
                                                               std::span<const DerivedDataKey> fingerprints) {
    if (resolve && fingerprints.empty()) {
        return importFailed("Model cache requires fingerprints for external inputs");
    }
    const auto key = makeDerivedDataKey(source, meta, platform, fingerprints);
    if (!key) {
        return importFailed("Cannot compute model cache key");
    }
    const auto stored = cache.read(*key);
    if (!stored) {
        return importFailed("Cannot read model cache at " + stored.error().path.string());
    }
    if (*stored) {
        if (auto model = readModelAsset(**stored)) {
            return CachedModel{std::move(*model), *key, true};
        }
    }
    auto model = importModel(source, meta.importer, resolve);
    if (!model) {
        return std::unexpected(std::move(model.error()));
    }
    const auto payload = writeModelAsset(*model);
    if (!payload) {
        return importFailed("Cannot serialize static model");
    }
    if (const auto result = cache.store(*key, *payload); !result) {
        return importFailed("Cannot write model cache at " + result.error().path.string());
    }
    return CachedModel{std::move(*model), *key, false};
}

} // namespace star::asset_pipeline
