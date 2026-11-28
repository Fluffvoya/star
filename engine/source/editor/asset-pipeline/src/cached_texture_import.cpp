#include "asset-pipeline/cached_texture_import.hpp"

#include <utility>

namespace star::asset_pipeline {
namespace {

std::string_view scalar(const serialization::ArchiveValue &field) {
    if (const auto *text = std::get_if<std::string>(&field.value)) {
        return *text;
    }
    if (const auto *plain = std::get_if<serialization::PlainScalar>(&field.value)) {
        return plain->text;
    }
    return {};
}

std::unexpected<CachedTextureError> error(CachedTextureErrorCode code, std::string detail) {
    return std::unexpected(CachedTextureError{code, std::move(detail)});
}

std::expected<TextureImportSettings, CachedTextureError> getSettings(const AssetMeta &meta) {
    if (meta.importer != "star::TextureImporter") {
        return error(CachedTextureErrorCode::InvalidMeta, "Importer");
    }
    TextureImportSettings settings;
    if (const auto usage = meta.settings.find("Usage"); usage != meta.settings.end()) {
        const auto text = scalar(usage->second);
        if (text == "Linear") {
            settings.usage = TextureUsage::Linear;
        } else if (text == "Normal") {
            settings.usage = TextureUsage::Normal;
        } else if (text != "Color") {
            return error(CachedTextureErrorCode::InvalidMeta, "Settings.Usage");
        }
    }
    if (const auto mips = meta.settings.find("GenerateMips"); mips != meta.settings.end()) {
        const auto text = scalar(mips->second);
        if (text != "true" && text != "false") {
            return error(CachedTextureErrorCode::InvalidMeta, "Settings.GenerateMips");
        }
        settings.generate_mips = text == "true";
    }
    if (const auto compress = meta.settings.find("BlockCompress"); compress != meta.settings.end()) {
        const auto text = scalar(compress->second);
        if (text != "true" && text != "false") {
            return error(CachedTextureErrorCode::InvalidMeta, "Settings.BlockCompress");
        }
        settings.block_compress = text == "true";
    }
    return settings;
}

} // namespace

std::expected<CachedTexture, CachedTextureError> importCachedTexture(std::span<const std::byte> source,
                                                                     const AssetMeta &meta, std::string_view platform,
                                                                     const DerivedDataCache &cache) {
    const auto settings = getSettings(meta);
    if (!settings) {
        return std::unexpected(settings.error());
    }
    const auto key = makeDerivedDataKey(source, meta, platform);
    if (!key) {
        return error(CachedTextureErrorCode::InvalidMeta, "Cannot compute texture cache key");
    }
    const auto stored = cache.read(*key);
    if (!stored) {
        return error(CachedTextureErrorCode::CacheIo, stored.error().path.string());
    }
    if (*stored) {
        auto imported = readTextureAsset(**stored);
        if (imported && imported->usage == settings->usage) {
            const auto &last = imported->mips.back();
            const bool expected_mips =
                settings->generate_mips ? last.width == 1 && last.height == 1 : imported->mips.size() == 1;
            // Payloads without block compression predate the format field and must be rebuilt.
            const bool expects_blocks = settings->block_compress && settings->usage != TextureUsage::Normal;
            const bool expected_format =
                expects_blocks ? imported->format != TextureFormat::Rgba8 : imported->format == TextureFormat::Rgba8;
            if (expected_mips && expected_format) {
                return CachedTexture{std::move(*imported), *key, true};
            }
        }
    }
    auto imported = importTexture(source, *settings);
    if (!imported) {
        return error(CachedTextureErrorCode::ImportFailed, std::move(imported.error().message));
    }
    const auto bytes = writeTextureAsset(*imported);
    if (!bytes) {
        return error(CachedTextureErrorCode::ImportFailed, "Cannot serialize texture mip chain");
    }
    if (const auto result = cache.store(*key, *bytes); !result) {
        return error(CachedTextureErrorCode::CacheIo, result.error().path.string());
    }
    return CachedTexture{std::move(*imported), *key, false};
}

} // namespace star::asset_pipeline
