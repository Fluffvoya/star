#pragma once

#include "asset-pipeline/export.hpp"
#include "asset/texture_asset.hpp"

#include <span>
#include <vector>

namespace star::asset_pipeline {

struct BlockCompressedTexture {
    TextureFormat format = TextureFormat::Bc1;
    std::vector<TextureMip> mips;
};

// Encodes RGBA8 mips into BC blocks. BC1 is used when every alpha channel is opaque, BC3 otherwise.
[[nodiscard]] STAR_ASSETPIPELINE_API BlockCompressedTexture compressTextureMips(std::span<const TextureMip> mips);

// Block encoders used by the importer. Mip payloads hold ceil(width / 4) * ceil(height / 4) blocks.
[[nodiscard]] STAR_ASSETPIPELINE_API std::vector<std::uint8_t> encodeBc1(const TextureMip &mip);
[[nodiscard]] STAR_ASSETPIPELINE_API std::vector<std::uint8_t> encodeBc3(const TextureMip &mip);

} // namespace star::asset_pipeline
