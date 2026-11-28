#pragma once

#include "asset/export.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace star {

enum class TextureColorSpace : std::uint8_t { Color, Linear, Normal };

// Rgba8 holds four bytes per pixel. Block formats hold BC blocks in row-major block order.
enum class TextureFormat : std::uint8_t { Rgba8, Bc1, Bc3 };

struct TextureMip {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // RGBA8 pixels or encoded block rows, depending on the owning texture format.
    std::vector<std::uint8_t> data;

    friend bool operator==(const TextureMip &, const TextureMip &) = default;
};

struct TextureAsset {
    TextureColorSpace usage = TextureColorSpace::Color;
    std::vector<TextureMip> mips;
    TextureFormat format = TextureFormat::Rgba8;

    friend bool operator==(const TextureAsset &, const TextureAsset &) = default;
};

// Bytes a mip payload occupies for the given format. Dimensions must be non-zero.
[[nodiscard]] STAR_ASSET_API std::size_t texturePayloadSize(TextureFormat format, std::uint32_t width,
                                                            std::uint32_t height) noexcept;

enum class TextureAssetError : std::uint8_t { InvalidHeader, UnsupportedVersion, InvalidMipChain };

[[nodiscard]] STAR_ASSET_API std::expected<std::vector<std::byte>, TextureAssetError>
writeTextureAsset(const TextureAsset &texture);

[[nodiscard]] STAR_ASSET_API std::expected<TextureAsset, TextureAssetError>
readTextureAsset(std::span<const std::byte> bytes);

} // namespace star
