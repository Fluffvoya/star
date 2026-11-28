#pragma once

#include "asset-pipeline/export.hpp"
#include "asset/texture_asset.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <string>

namespace star::asset_pipeline {

using TextureUsage = TextureColorSpace;
using TextureMip = star::TextureMip;
using ImportedTexture = TextureAsset;

struct TextureImportSettings {
    TextureUsage usage = TextureUsage::Color;
    bool generate_mips = true;
    // Opaque colour and linear data become BC1; textures with alpha become BC3. Normals stay RGBA8.
    bool block_compress = true;
};

struct TextureImportError {
    std::string message;
};

// Imports an LDR image into top-left origin. Color mip filtering occurs in linear light. When block
// compression is enabled the result holds BC1 or BC3 mips instead of RGBA8 pixels.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<ImportedTexture, TextureImportError>
importTexture(std::span<const std::byte> source, TextureImportSettings settings = {});

} // namespace star::asset_pipeline
