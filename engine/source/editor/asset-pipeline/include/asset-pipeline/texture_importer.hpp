#pragma once

#include "asset-pipeline/export.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace star::asset_pipeline {

enum class TextureUsage : std::uint8_t { Color, Linear, Normal };

struct TextureImportSettings {
    TextureUsage usage = TextureUsage::Color;
    bool generate_mips = true;
};

struct TextureMip {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> rgba;
};

struct ImportedTexture {
    TextureUsage usage;
    std::vector<TextureMip> mips;
};

struct TextureImportError {
    std::string message;
};

// Imports an LDR image into top-left-origin RGBA8. Color mip filtering occurs in linear light.
[[nodiscard]] STAR_ASSETPIPELINE_API std::expected<ImportedTexture, TextureImportError>
importTexture(std::span<const std::byte> source, TextureImportSettings settings = {});

} // namespace star::asset_pipeline
