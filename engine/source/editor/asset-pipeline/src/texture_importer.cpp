#include "asset-pipeline/texture_importer.hpp"

#include "asset-pipeline/texture_compress.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace star::asset_pipeline {
namespace {

constexpr std::size_t channels = 4;

double fromSrgb(double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); }

double toSrgb(double value) { return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055; }

std::uint8_t toByte(double value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

std::array<double, channels> getPixel(const std::uint8_t *source, TextureUsage usage) {
    std::array<double, channels> pixel{};
    for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
        pixel[channel] = source[channel] / 255.0;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (usage == TextureUsage::Color) {
            pixel[channel] = fromSrgb(pixel[channel]);
        } else if (usage == TextureUsage::Normal) {
            pixel[channel] = pixel[channel] * 2.0 - 1.0;
        }
    }
    return pixel;
}

void putPixel(std::uint8_t *destination, std::array<double, channels> pixel, TextureUsage usage) {
    if (usage == TextureUsage::Normal) {
        const double length = std::hypot(pixel[0], pixel[1], pixel[2]);
        if (length > 1e-8) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                pixel[channel] = pixel[channel] / length * 0.5 + 0.5;
            }
        } else {
            pixel[0] = 0.5;
            pixel[1] = 0.5;
            pixel[2] = 1.0;
        }
    } else if (usage == TextureUsage::Color) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            pixel[channel] = toSrgb(pixel[channel]);
        }
    }
    for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
        destination[channel] = toByte(pixel[channel]);
    }
}

TextureMip downsample(const TextureMip &source, TextureUsage usage) {
    TextureMip mip{std::max(1u, source.width / 2), std::max(1u, source.height / 2), {}};
    mip.data.resize(static_cast<std::size_t>(mip.width) * mip.height * channels);
    const double x_scale = static_cast<double>(source.width) / mip.width;
    const double y_scale = static_cast<double>(source.height) / mip.height;
    for (std::uint32_t y = 0; y < mip.height; ++y) {
        for (std::uint32_t x = 0; x < mip.width; ++x) {
            const double left = x * x_scale;
            const double right = (x + 1) * x_scale;
            const double top = y * y_scale;
            const double bottom = (y + 1) * y_scale;
            std::array<double, channels> filtered{};
            for (auto sy = static_cast<std::uint32_t>(top); sy < source.height && sy < bottom; ++sy) {
                for (auto sx = static_cast<std::uint32_t>(left); sx < source.width && sx < right; ++sx) {
                    const double x_weight = std::min(right, static_cast<double>(sx + 1)) - std::max(left, double(sx));
                    const double y_weight = std::min(bottom, static_cast<double>(sy + 1)) - std::max(top, double(sy));
                    const auto index = (static_cast<std::size_t>(sy) * source.width + sx) * channels;
                    const auto pixel = getPixel(source.data.data() + index, usage);
                    for (std::size_t channel = 0; channel < channels; ++channel) {
                        filtered[channel] += pixel[channel] * x_weight * y_weight;
                    }
                }
            }
            for (double &value : filtered) {
                value /= x_scale * y_scale;
            }
            const auto index = (static_cast<std::size_t>(y) * mip.width + x) * channels;
            putPixel(mip.data.data() + index, filtered, usage);
        }
    }
    return mip;
}

} // namespace

std::expected<ImportedTexture, TextureImportError> importTexture(std::span<const std::byte> source,
                                                                 TextureImportSettings settings) {
    if (source.empty() || source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(TextureImportError{"Invalid image input size"});
    }

    int width = 0;
    int height = 0;
    const auto *input = reinterpret_cast<const stbi_uc *>(source.data());
    const auto size = static_cast<int>(source.size());
    using Pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    Pixels pixels(stbi_load_from_memory(input, size, &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    if (!pixels) {
        const char *reason = stbi_failure_reason();
        return std::unexpected(TextureImportError{reason ? reason : "Image decode failed"});
    }

    TextureMip base{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
    const auto pixel_count = static_cast<std::size_t>(base.width) * base.height;
    base.data.assign(pixels.get(), pixels.get() + pixel_count * channels);
    pixels.reset();

    std::vector<TextureMip> mips;
    mips.push_back(std::move(base));
    while (settings.generate_mips && (mips.back().width > 1 || mips.back().height > 1)) {
        mips.push_back(downsample(mips.back(), settings.usage));
    }

    ImportedTexture texture{settings.usage, {}, TextureFormat::Rgba8};
    if (settings.block_compress && settings.usage != TextureUsage::Normal) {
        auto compressed = compressTextureMips(mips);
        texture.format = compressed.format;
        texture.mips = std::move(compressed.mips);
    } else {
        texture.mips = std::move(mips);
    }
    return texture;
}

} // namespace star::asset_pipeline
