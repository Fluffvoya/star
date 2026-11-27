#pragma once

#include "asset-pipeline/export.hpp"
#include "core/guid.hpp"
#include "serialization/archive.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace star::asset_pipeline {

struct SubAsset {
    std::string name;
    std::string type;
    Guid guid;
    serialization::ArchiveValue::Mapping extensions;
    friend bool operator==(const SubAsset &, const SubAsset &) = default;
};

struct AssetMeta {
    Guid guid;
    std::string importer;
    std::uint32_t importer_version = 0;
    serialization::ArchiveValue::Mapping settings;
    std::vector<SubAsset> sub_assets;
    serialization::ArchiveValue::Mapping extensions;
    friend bool operator==(const AssetMeta &, const AssetMeta &) = default;
};

enum class AssetDatabaseErrorCode : std::uint8_t {
    InvalidMeta,
    IoFailure,
    MissingSource,
    DuplicateGuid,
};

struct AssetDatabaseError {
    AssetDatabaseErrorCode code;
    std::filesystem::path path;
    std::string field;
};

struct AssetLocation {
    std::filesystem::path source;
    std::optional<std::size_t> sub_asset_index;
};

[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<AssetMeta, AssetDatabaseError> readAssetMeta(std::string_view yaml);

[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<std::string, AssetDatabaseError>
writeAssetMeta(const AssetMeta &meta);

// Paths returned by the database are relative to the scanned content root.
// A failed scan preserves the previous snapshot. A successful scan invalidates lookup pointers.
class STAR_ASSETPIPELINE_API AssetDatabase final {
  public:
    [[nodiscard]] std::expected<void, AssetDatabaseError> scan(const std::filesystem::path &content_root);
    [[nodiscard]] const AssetMeta *findMeta(const std::filesystem::path &source) const;
    [[nodiscard]] const AssetLocation *findAsset(Guid guid) const;

  private:
    std::map<std::filesystem::path, AssetMeta> m_metadata;
    std::map<Guid, AssetLocation> m_locations;
};

} // namespace star::asset_pipeline
