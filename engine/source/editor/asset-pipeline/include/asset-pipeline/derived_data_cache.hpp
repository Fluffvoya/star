#pragma once

#include "asset-pipeline/asset_database.hpp"
#include "asset-pipeline/export.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace star::asset_pipeline {

using DerivedDataKey = std::array<std::byte, 16>;

[[nodiscard]] STAR_ASSETPIPELINE_API DerivedDataKey fingerprintDerivedDataInput(std::span<const std::byte> bytes);

enum class DerivedDataErrorCode { InvalidInput, IoFailure };

struct DerivedDataError {
    DerivedDataErrorCode code;
    std::filesystem::path path;
};

// Dependencies must be supplied in a stable order and include every external input
// that can affect the result, such as external glTF buffers and images.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<DerivedDataKey, DerivedDataError>
makeDerivedDataKey(std::span<const std::byte> source, const AssetMeta &meta, std::string_view platform,
                   std::span<const DerivedDataKey> dependencies = {});

class STAR_ASSETPIPELINE_API DerivedDataCache final {
  public:
    explicit DerivedDataCache(std::filesystem::path root);

    // A missing file is a cache miss. A successful read may contain an empty payload.
    [[nodiscard]] std::expected<std::optional<std::vector<std::byte>>, DerivedDataError>
    read(const DerivedDataKey &key) const;

    // Writers publish a complete file by renaming within the cache directory.
    [[nodiscard]] std::expected<void, DerivedDataError> store(const DerivedDataKey &key,
                                                              std::span<const std::byte> bytes) const;

  private:
    std::filesystem::path m_root;
};

} // namespace star::asset_pipeline
