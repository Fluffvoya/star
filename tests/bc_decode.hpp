#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace star::test {

// Minimal BC1/BC3 decoders used to verify importer output in tests.

inline std::array<std::uint8_t, 4> unpack565(std::uint16_t value, std::uint8_t alpha = 255) {
    return {static_cast<std::uint8_t>(((value >> 11) & 0x1FU) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 5) & 0x3FU) * 255 / 63),
            static_cast<std::uint8_t>((value & 0x1FU) * 255 / 31), alpha};
}

inline std::vector<std::uint8_t> decodeBc1(std::span<const std::uint8_t> blocks, std::uint32_t width,
                                           std::uint32_t height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    const auto columns = (width + 3) / 4;
    const auto rows = (height + 3) / 4;
    for (std::uint32_t block_y = 0; block_y < rows; ++block_y) {
        for (std::uint32_t block_x = 0; block_x < columns; ++block_x) {
            const auto offset = (static_cast<std::size_t>(block_y) * columns + block_x) * 8;
            const auto c0 = static_cast<std::uint16_t>(blocks[offset] | (blocks[offset + 1] << 8));
            const auto c1 = static_cast<std::uint16_t>(blocks[offset + 2] | (blocks[offset + 3] << 8));
            std::array<std::array<std::uint8_t, 4>, 4> palette{};
            palette[0] = unpack565(c0);
            palette[1] = unpack565(c1);
            if (c0 > c1) {
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    palette[2][channel] =
                        static_cast<std::uint8_t>((2 * palette[0][channel] + palette[1][channel]) / 3);
                    palette[3][channel] =
                        static_cast<std::uint8_t>((palette[0][channel] + 2 * palette[1][channel]) / 3);
                }
                palette[2][3] = 255;
                palette[3][3] = 255;
            } else {
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    palette[2][channel] = static_cast<std::uint8_t>((palette[0][channel] + palette[1][channel]) / 2);
                    palette[3][channel] = 0;
                }
                palette[2][3] = 255;
                palette[3][3] = 0;
            }
            const auto indices = static_cast<std::uint32_t>(blocks[offset + 4]) |
                                 (static_cast<std::uint32_t>(blocks[offset + 5]) << 8) |
                                 (static_cast<std::uint32_t>(blocks[offset + 6]) << 16) |
                                 (static_cast<std::uint32_t>(blocks[offset + 7]) << 24);
            for (std::uint32_t y = 0; y < 4; ++y) {
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const auto pixel_x = block_x * 4 + x;
                    const auto pixel_y = block_y * 4 + y;
                    if (pixel_x >= width || pixel_y >= height) {
                        continue;
                    }
                    const auto index = (indices >> ((y * 4 + x) * 2)) & 0x3U;
                    const auto &color = palette[index];
                    const auto target = (static_cast<std::size_t>(pixel_y) * width + pixel_x) * 4;
                    std::copy(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(target));
                }
            }
        }
    }
    return pixels;
}

inline std::vector<std::uint8_t> decodeBc3(std::span<const std::uint8_t> blocks, std::uint32_t width,
                                           std::uint32_t height) {
    const auto columns = (width + 3) / 4;
    const auto rows = (height + 3) / 4;
    // Every BC3 colour block starts eight bytes after its own alpha block, so decode one block at a time.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::uint32_t block_y = 0; block_y < rows; ++block_y) {
        for (std::uint32_t block_x = 0; block_x < columns; ++block_x) {
            const auto offset = (static_cast<std::size_t>(block_y) * columns + block_x) * 16;
            const auto color_block = blocks.subspan(offset + 8, 8);
            const auto color = decodeBc1(color_block, 4, 4);
            const auto a0 = blocks[offset];
            const auto a1 = blocks[offset + 1];
            std::array<std::uint8_t, 8> alpha{};
            alpha[0] = a0;
            alpha[1] = a1;
            if (a0 > a1) {
                for (std::uint8_t index = 2; index < 8; ++index) {
                    alpha[index] = static_cast<std::uint8_t>(
                        (static_cast<std::uint32_t>(8 - index) * a0 + static_cast<std::uint32_t>(index - 1) * a1) / 7);
                }
            } else {
                for (std::uint8_t index = 2; index < 6; ++index) {
                    alpha[index] = static_cast<std::uint8_t>(
                        (static_cast<std::uint32_t>(6 - index) * a0 + static_cast<std::uint32_t>(index - 1) * a1) / 5);
                }
                alpha[6] = 0;
                alpha[7] = 255;
            }
            std::uint64_t indices = 0;
            for (std::uint32_t byte = 0; byte < 6; ++byte) {
                indices |= static_cast<std::uint64_t>(blocks[offset + 2 + byte]) << (byte * 8);
            }
            for (std::uint32_t y = 0; y < 4; ++y) {
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const auto pixel_x = block_x * 4 + x;
                    const auto pixel_y = block_y * 4 + y;
                    if (pixel_x >= width || pixel_y >= height) {
                        continue;
                    }
                    const auto alpha_index = (indices >> ((y * 4 + x) * 3)) & 0x7U;
                    const auto target = (static_cast<std::size_t>(pixel_y) * width + pixel_x) * 4;
                    std::copy_n(color.begin() + static_cast<std::ptrdiff_t>((y * 4 + x) * 4), 3,
                                pixels.begin() + static_cast<std::ptrdiff_t>(target));
                    pixels[target + 3] = alpha[alpha_index];
                }
            }
        }
    }
    return pixels;
}

} // namespace star::test
