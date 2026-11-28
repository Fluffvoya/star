#include "asset-pipeline/cached_texture_import.hpp"
#include "asset-pipeline/texture_compress.hpp"
#include "asset-pipeline/texture_importer.hpp"
#include "bc_decode.hpp"
#include "test_support.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <vector>

namespace {

using namespace star::asset_pipeline;

std::vector<std::uint8_t> makeTga(std::initializer_list<std::array<std::uint8_t, 3>> pixels) {
    std::vector<std::uint8_t> image(18, 0);
    image[2] = 2; // Uncompressed true-color TGA.
    image[12] = static_cast<std::uint8_t>(pixels.size());
    image[14] = 1;
    image[16] = 24;
    image[17] = 32; // Top-left origin.
    for (const auto &pixel : pixels) {
        image.insert(image.end(), {pixel[2], pixel[1], pixel[0]});
    }
    return image;
}

} // namespace

STAR_TEST(texture_importer_filters_srgb_in_linear_light_and_includes_odd_edges) {
    const auto source = makeTga({{0, 0, 0}, {0, 0, 0}, {255, 255, 255}});
    const auto bytes = std::as_bytes(std::span(source));
    const auto color = importTexture(bytes, {TextureUsage::Color, true, false});
    STAR_CHECK(color.has_value());
    STAR_CHECK(color->mips.size() == 2);
    STAR_CHECK(color->mips[0].width == 3);
    STAR_CHECK(color->mips[0].data[8] == 255);
    STAR_CHECK(color->mips[1].width == 1);
    STAR_CHECK(color->mips[1].height == 1);
    STAR_CHECK(color->mips[1].data[0] == 156);
    STAR_CHECK(color->mips[1].data[3] == 255);

    const auto linear = importTexture(bytes, {TextureUsage::Linear, true, false});
    STAR_CHECK(linear.has_value());
    STAR_CHECK(linear->mips[1].data[0] == 85);

    const auto base_only = importTexture(bytes, {TextureUsage::Linear, false, false});
    STAR_CHECK(base_only.has_value());
    STAR_CHECK(base_only->mips.size() == 1);
}

STAR_TEST(texture_importer_renormalizes_normal_mips) {
    const auto source = makeTga({{255, 128, 128}, {128, 128, 255}});
    const auto texture = importTexture(std::as_bytes(std::span(source)), {TextureUsage::Normal, true, true});
    STAR_CHECK(texture.has_value());
    const auto &pixel = texture->mips.at(1).data;
    const auto x = pixel[0] / 255.0 * 2.0 - 1.0;
    const auto y = pixel[1] / 255.0 * 2.0 - 1.0;
    const auto z = pixel[2] / 255.0 * 2.0 - 1.0;
    STAR_CHECK(std::abs(std::hypot(x, y, z) - 1.0) < 0.01);
    STAR_CHECK(x > 0.7);
    STAR_CHECK(z > 0.7);
}

STAR_TEST(texture_importer_rejects_unknown_input) {
    const std::array data{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const auto result = importTexture(data);
    STAR_CHECK(!result.has_value());
    STAR_CHECK(!result.error().message.empty());
}

STAR_TEST(texture_importer_preserves_png_alpha) {
    constexpr std::uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                                    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
                                    0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78,
                                    0x9c, 0x63, 0xe0, 0x12, 0x91, 0xd3, 0x00, 0x00, 0x00, 0xcd, 0x00, 0x65, 0x6a, 0x99,
                                    0x84, 0x42, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    const auto result = importTexture(std::as_bytes(std::span(png)), {TextureUsage::Color, true, false});
    STAR_CHECK(result.has_value());
    STAR_CHECK(result->mips.size() == 1);
    const std::vector<std::uint8_t> expected{10, 20, 30, 40};
    STAR_CHECK(result->mips[0].data == expected);
}

STAR_TEST(texture_importer_caches_versioned_runtime_payload_and_recovers_corruption) {
    struct TemporaryCache {
        std::filesystem::path path = std::filesystem::temp_directory_path() / star::Guid::generate().toString();
        ~TemporaryCache() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } directory;
    const DerivedDataCache cache(directory.path);
    const auto source = makeTga({{0, 0, 0}, {255, 255, 255}});
    const auto bytes = std::as_bytes(std::span(source));
    auto meta = readAssetMeta(R"(
StarFile: Meta
Guid: 9b2d4c6e8f0a1b3c5d7e9f1a2b3c4d5e
Importer: star::TextureImporter
ImporterVersion: 1
Settings: {Usage: Linear, GenerateMips: true, BlockCompress: false}
)")
                    .value();

    const auto first = importCachedTexture(bytes, meta, "linux", cache);
    STAR_CHECK(first.has_value());
    STAR_CHECK(!first->cache_hit);
    STAR_CHECK(first->texture.mips.at(1).data.at(0) == 128);

    const auto hit = importCachedTexture(bytes, meta, "linux", cache);
    STAR_CHECK(hit.has_value());
    STAR_CHECK(hit->cache_hit);
    STAR_CHECK(hit->texture == first->texture);
    const auto stored = cache.read(first->key).value().value();
    STAR_CHECK(star::readTextureAsset(stored).value() == first->texture);

    STAR_CHECK(cache.store(first->key, {}).has_value());
    const auto repaired = importCachedTexture(bytes, meta, "linux", cache);
    STAR_CHECK(repaired.has_value());
    STAR_CHECK(!repaired->cache_hit);
    STAR_CHECK(repaired->texture == first->texture);

    meta.settings["GenerateMips"] = star::serialization::ArchiveValue{star::serialization::PlainScalar{"false"}};
    const auto new_settings = importCachedTexture(bytes, meta, "linux", cache);
    STAR_CHECK(new_settings.has_value());
    STAR_CHECK(!new_settings->cache_hit);
    STAR_CHECK(new_settings->key != first->key);
    STAR_CHECK(new_settings->texture.mips.size() == 1);
}

STAR_TEST(texture_importer_block_compresses_opaque_and_alpha_images) {
    const auto opaque = makeTga({{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}});
    const auto color = importTexture(std::as_bytes(std::span(opaque)), {TextureUsage::Color, false, true});
    STAR_CHECK(color.has_value());
    STAR_CHECK(color->format == star::TextureFormat::Bc1);
    STAR_CHECK(color->mips.at(0).data.size() == 8);
    const auto color_pixels =
        star::test::decodeBc1(color->mips.at(0).data, color->mips.at(0).width, color->mips.at(0).height);
    STAR_CHECK(color_pixels[0] > 240);
    STAR_CHECK(color_pixels[1] < 16);
    STAR_CHECK(color_pixels[2] < 16);
    STAR_CHECK(color_pixels[3] == 255);

    constexpr std::uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                                    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
                                    0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78,
                                    0x9c, 0x63, 0xe0, 0x12, 0x91, 0xd3, 0x00, 0x00, 0x00, 0xcd, 0x00, 0x65, 0x6a, 0x99,
                                    0x84, 0x42, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    const auto alpha = importTexture(std::as_bytes(std::span(png)), {TextureUsage::Color, true, true});
    STAR_CHECK(alpha.has_value());
    STAR_CHECK(alpha->format == star::TextureFormat::Bc3);
    STAR_CHECK(alpha->mips.at(0).data.size() == 16);
    const auto alpha_pixels =
        star::test::decodeBc3(alpha->mips.at(0).data, alpha->mips.at(0).width, alpha->mips.at(0).height);
    STAR_CHECK(std::abs(static_cast<int>(alpha_pixels[3]) - 40) <= 2);
}

STAR_TEST(texture_importer_keeps_normal_maps_uncompressed) {
    const auto source = makeTga({{255, 128, 128}, {128, 128, 255}});
    const auto texture = importTexture(std::as_bytes(std::span(source)), {TextureUsage::Normal, true, true});
    STAR_CHECK(texture.has_value());
    STAR_CHECK(texture->format == star::TextureFormat::Rgba8);
    STAR_CHECK(texture->mips.at(1).data.size() == 4);
}
