#include "asset/texture_asset.hpp"

#include "asset_binary.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>

namespace star {
namespace {

constexpr std::array magic{std::byte{'S'}, std::byte{'T'}, std::byte{'E'}, std::byte{'X'}};
constexpr std::uint32_t version = 1;
constexpr std::uint32_t max_mips = std::numeric_limits<std::uint32_t>::digits;

std::optional<std::size_t> rgbaSize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || std::uint64_t(width) * height > std::numeric_limits<std::size_t>::max() / 4) {
        return std::nullopt;
    }
    return std::size_t(width) * height * 4;
}

bool follows(const TextureMip &previous, std::uint32_t width, std::uint32_t height) {
    return (previous.width != 1 || previous.height != 1) && width == std::max(1U, previous.width / 2) &&
           height == std::max(1U, previous.height / 2);
}

using asset_binary::appendUInt32;
using asset_binary::takeUInt32;

} // namespace

std::expected<std::vector<std::byte>, TextureAssetError> writeTextureAsset(const TextureAsset &texture) {
    const auto usage = static_cast<std::uint32_t>(texture.usage);
    if (usage > static_cast<std::uint32_t>(TextureColorSpace::Normal)) {
        return std::unexpected(TextureAssetError::InvalidHeader);
    }
    if (texture.mips.empty() || texture.mips.size() > max_mips) {
        return std::unexpected(TextureAssetError::InvalidMipChain);
    }

    std::vector<std::byte> bytes(magic.begin(), magic.end());
    appendUInt32(bytes, version);
    appendUInt32(bytes, usage);
    appendUInt32(bytes, static_cast<std::uint32_t>(texture.mips.size()));
    const TextureMip *previous = nullptr;
    for (const auto &mip : texture.mips) {
        const auto size = rgbaSize(mip.width, mip.height);
        if (!size || *size != mip.rgba.size() || (previous && !follows(*previous, mip.width, mip.height))) {
            return std::unexpected(TextureAssetError::InvalidMipChain);
        }
        appendUInt32(bytes, mip.width);
        appendUInt32(bytes, mip.height);
        const auto pixels = std::as_bytes(std::span(mip.rgba));
        bytes.insert(bytes.end(), pixels.begin(), pixels.end());
        previous = &mip;
    }
    return bytes;
}

std::expected<TextureAsset, TextureAssetError> readTextureAsset(std::span<const std::byte> bytes) {
    if (bytes.size() < magic.size() || !std::ranges::equal(bytes.first(magic.size()), magic)) {
        return std::unexpected(TextureAssetError::InvalidHeader);
    }
    bytes = bytes.subspan(magic.size());
    const auto file_version = takeUInt32(bytes);
    const auto usage = takeUInt32(bytes);
    const auto count = takeUInt32(bytes);
    if (!file_version || !usage || !count || *usage > static_cast<std::uint32_t>(TextureColorSpace::Normal)) {
        return std::unexpected(TextureAssetError::InvalidHeader);
    }
    if (*file_version != version) {
        return std::unexpected(TextureAssetError::UnsupportedVersion);
    }
    if (*count == 0 || *count > max_mips) {
        return std::unexpected(TextureAssetError::InvalidMipChain);
    }
    TextureAsset texture{static_cast<TextureColorSpace>(*usage), {}};
    texture.mips.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        const auto width = takeUInt32(bytes);
        const auto height = takeUInt32(bytes);
        if (!width || !height) {
            return std::unexpected(TextureAssetError::InvalidMipChain);
        }
        const auto size = rgbaSize(*width, *height);
        if (!size || *size > bytes.size() || *size > std::vector<std::uint8_t>().max_size() ||
            (!texture.mips.empty() && !follows(texture.mips.back(), *width, *height))) {
            return std::unexpected(TextureAssetError::InvalidMipChain);
        }
        auto &mip = texture.mips.emplace_back(*width, *height, std::vector<std::uint8_t>(*size));
        std::memcpy(mip.rgba.data(), bytes.data(), *size);
        bytes = bytes.subspan(*size);
    }
    if (!bytes.empty()) {
        return std::unexpected(TextureAssetError::InvalidMipChain);
    }
    return texture;
}

} // namespace star
