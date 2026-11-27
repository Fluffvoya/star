#pragma once

#include "asset/export.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace star {

enum class TextureColorSpace : std::uint8_t { Color, Linear, Normal };

struct TextureMip {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    friend bool operator==(const TextureMip &, const TextureMip &) = default;
};

struct TextureAsset {
    TextureColorSpace usage = TextureColorSpace::Color;
    std::vector<TextureMip> mips;

    friend bool operator==(const TextureAsset &, const TextureAsset &) = default;
};

enum class TextureAssetError : std::uint8_t { InvalidHeader, UnsupportedVersion, InvalidMipChain };

[[nodiscard]] STAR_ASSET_API std::expected<std::vector<std::byte>, TextureAssetError>
writeTextureAsset(const TextureAsset &texture);

[[nodiscard]] STAR_ASSET_API std::expected<TextureAsset, TextureAssetError>
readTextureAsset(std::span<const std::byte> bytes);

} // namespace star
