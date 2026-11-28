#include "asset-pipeline/texture_compress.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace star::asset_pipeline {
namespace {

constexpr std::uint32_t block_edge = 4;
constexpr std::uint32_t block_pixels = block_edge * block_edge;

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Block {
    std::array<Rgba, block_pixels> pixels{};
};

std::uint32_t blockCount(std::uint32_t size) { return (size + block_edge - 1) / block_edge; }

Block readBlock(const TextureMip &mip, std::uint32_t block_x, std::uint32_t block_y) {
    Block block;
    for (std::uint32_t y = 0; y < block_edge; ++y) {
        for (std::uint32_t x = 0; x < block_edge; ++x) {
            const auto pixel_x = std::min(block_x * block_edge + x, mip.width - 1);
            const auto pixel_y = std::min(block_y * block_edge + y, mip.height - 1);
            const auto offset = (static_cast<std::size_t>(pixel_y) * mip.width + pixel_x) * 4;
            auto &pixel = block.pixels[y * block_edge + x];
            pixel = {mip.data[offset], mip.data[offset + 1], mip.data[offset + 2], mip.data[offset + 3]};
        }
    }
    return block;
}

std::uint16_t toRgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((static_cast<std::uint16_t>(r) & 0xF8U) << 8) |
                                      ((static_cast<std::uint16_t>(g) & 0xFCU) << 3) |
                                      ((static_cast<std::uint16_t>(b) & 0xF8U) >> 3));
}

Rgba fromRgb565(std::uint16_t value) {
    const auto r = static_cast<std::uint8_t>(((value >> 11) & 0x1FU) * 255 / 31);
    const auto g = static_cast<std::uint8_t>(((value >> 5) & 0x3FU) * 255 / 63);
    const auto b = static_cast<std::uint8_t>((value & 0x1FU) * 255 / 31);
    return {r, g, b, 255};
}

std::int32_t distance(const Rgba &left, const Rgba &right) {
    const auto dr = static_cast<std::int32_t>(left.r) - right.r;
    const auto dg = static_cast<std::int32_t>(left.g) - right.g;
    const auto db = static_cast<std::int32_t>(left.b) - right.b;
    return dr * dr + dg * dg + db * db;
}

std::pair<std::uint16_t, std::uint16_t> blockEndpoints(const Block &block) {
    std::uint8_t min_r = 255;
    std::uint8_t min_g = 255;
    std::uint8_t min_b = 255;
    std::uint8_t max_r = 0;
    std::uint8_t max_g = 0;
    std::uint8_t max_b = 0;
    for (const auto &pixel : block.pixels) {
        min_r = std::min(min_r, pixel.r);
        min_g = std::min(min_g, pixel.g);
        min_b = std::min(min_b, pixel.b);
        max_r = std::max(max_r, pixel.r);
        max_g = std::max(max_g, pixel.g);
        max_b = std::max(max_b, pixel.b);
    }
    auto high = toRgb565(max_r, max_g, max_b);
    auto low = toRgb565(min_r, min_g, min_b);
    if (high < low) {
        std::swap(high, low);
    }
    if (high == low) {
        // Force the four-colour mode so every index decodes to the same value.
        high = high == 0xFFFFU ? static_cast<std::uint16_t>(high - 1) : static_cast<std::uint16_t>(high + 1);
    }
    return {high, low};
}

std::array<std::uint8_t, block_pixels> colorIndices(const Block &block, std::uint16_t high, std::uint16_t low) {
    const Rgba colors[4]{
        fromRgb565(high),
        fromRgb565(low),
        {static_cast<std::uint8_t>((2 * fromRgb565(high).r + fromRgb565(low).r) / 3),
         static_cast<std::uint8_t>((2 * fromRgb565(high).g + fromRgb565(low).g) / 3),
         static_cast<std::uint8_t>((2 * fromRgb565(high).b + fromRgb565(low).b) / 3), 255},
        {static_cast<std::uint8_t>((fromRgb565(high).r + 2 * fromRgb565(low).r) / 3),
         static_cast<std::uint8_t>((fromRgb565(high).g + 2 * fromRgb565(low).g) / 3),
         static_cast<std::uint8_t>((fromRgb565(high).b + 2 * fromRgb565(low).b) / 3), 255},
    };
    std::array<std::uint8_t, block_pixels> indices{};
    for (std::size_t index = 0; index < block.pixels.size(); ++index) {
        std::int32_t best = std::numeric_limits<std::int32_t>::max();
        for (std::uint8_t candidate = 0; candidate < 4; ++candidate) {
            const auto current = distance(block.pixels[index], colors[candidate]);
            if (current < best) {
                best = current;
                indices[index] = candidate;
            }
        }
    }
    return indices;
}

void appendColorBlock(std::vector<std::uint8_t> &output, const Block &block) {
    const auto [high, low] = blockEndpoints(block);
    const auto indices = colorIndices(block, high, low);
    std::uint32_t packed = 0;
    for (std::size_t index = 0; index < indices.size(); ++index) {
        packed |= static_cast<std::uint32_t>(indices[index]) << (index * 2);
    }
    output.push_back(static_cast<std::uint8_t>(high & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(high >> 8));
    output.push_back(static_cast<std::uint8_t>(low & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(low >> 8));
    output.push_back(static_cast<std::uint8_t>(packed & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((packed >> 8) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((packed >> 16) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((packed >> 24) & 0xFFU));
}

void appendAlphaBlock(std::vector<std::uint8_t> &output, const Block &block) {
    std::uint8_t low = 255;
    std::uint8_t high = 0;
    for (const auto &pixel : block.pixels) {
        low = std::min(low, pixel.a);
        high = std::max(high, pixel.a);
    }
    std::array<std::uint8_t, 8> palette{};
    palette[0] = high;
    palette[1] = low;
    for (std::uint8_t index = 2; index < 8; ++index) {
        const auto numerator =
            static_cast<std::uint32_t>(8 - index) * high + static_cast<std::uint32_t>(index - 1) * low;
        palette[index] = static_cast<std::uint8_t>(numerator / 7);
    }
    std::uint64_t packed = 0;
    for (std::size_t index = 0; index < block.pixels.size(); ++index) {
        std::uint8_t best = 0;
        std::int32_t best_distance = std::numeric_limits<std::int32_t>::max();
        for (std::uint8_t candidate = 0; candidate < palette.size(); ++candidate) {
            const auto current = std::abs(static_cast<std::int32_t>(block.pixels[index].a) - palette[candidate]);
            if (current < best_distance) {
                best_distance = current;
                best = candidate;
            }
        }
        packed |= static_cast<std::uint64_t>(best) << (index * 3);
    }
    output.push_back(high);
    output.push_back(low);
    for (std::uint32_t byte = 0; byte < 6; ++byte) {
        output.push_back(static_cast<std::uint8_t>((packed >> (byte * 8)) & 0xFFU));
    }
}

} // namespace

std::vector<std::uint8_t> encodeBc1(const TextureMip &mip) {
    const auto columns = blockCount(mip.width);
    const auto rows = blockCount(mip.height);
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(columns) * rows * 8);
    for (std::uint32_t y = 0; y < rows; ++y) {
        for (std::uint32_t x = 0; x < columns; ++x) {
            appendColorBlock(output, readBlock(mip, x, y));
        }
    }
    return output;
}

std::vector<std::uint8_t> encodeBc3(const TextureMip &mip) {
    const auto columns = blockCount(mip.width);
    const auto rows = blockCount(mip.height);
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(columns) * rows * 16);
    for (std::uint32_t y = 0; y < rows; ++y) {
        for (std::uint32_t x = 0; x < columns; ++x) {
            const auto block = readBlock(mip, x, y);
            appendAlphaBlock(output, block);
            appendColorBlock(output, block);
        }
    }
    return output;
}

BlockCompressedTexture compressTextureMips(std::span<const TextureMip> mips) {
    bool transparent = false;
    for (const auto &mip : mips) {
        for (std::size_t index = 3; index < mip.data.size(); index += 4) {
            if (mip.data[index] != 255) {
                transparent = true;
                break;
            }
        }
        if (transparent) {
            break;
        }
    }
    BlockCompressedTexture result;
    result.format = transparent ? TextureFormat::Bc3 : TextureFormat::Bc1;
    result.mips.reserve(mips.size());
    for (const auto &mip : mips) {
        TextureMip compressed{mip.width, mip.height, {}};
        compressed.data = transparent ? encodeBc3(mip) : encodeBc1(mip);
        result.mips.push_back(std::move(compressed));
    }
    return result;
}

} // namespace star::asset_pipeline
