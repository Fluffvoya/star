#include "asset/model_asset.hpp"

#include "asset_binary.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace star {
namespace {

using asset_binary::appendUInt32;
using asset_binary::takeUInt32;

constexpr std::array magic{std::byte{'S'}, std::byte{'M'}, std::byte{'D'}, std::byte{'L'}};
constexpr std::uint32_t version = 1;
constexpr std::uint32_t no_texture = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t vertex_bytes = 8 * sizeof(float);
constexpr std::size_t min_part_bytes = 3 * vertex_bytes + 8 * sizeof(std::uint32_t);

std::optional<std::span<const std::byte>> takeFields(std::span<const std::byte> &bytes) {
    const auto size = takeUInt32(bytes);
    if (!size || *size > bytes.size()) {
        return std::nullopt;
    }
    const auto result = bytes.first(*size);
    bytes = bytes.subspan(*size);
    return result;
}

bool appendFields(std::vector<std::byte> &bytes, std::span<const std::byte> fields) {
    if (fields.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    appendUInt32(bytes, static_cast<std::uint32_t>(fields.size()));
    bytes.insert(bytes.end(), fields.begin(), fields.end());
    return true;
}

bool readString(std::span<const std::byte> &bytes, std::string &text) {
    const auto fields = takeFields(bytes);
    if (!fields) {
        return false;
    }
    text.assign(reinterpret_cast<const char *>(fields->data()), fields->size());
    return true;
}

bool appendFloats(std::vector<std::byte> &bytes, std::span<const float> values) {
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
        appendUInt32(bytes, std::bit_cast<std::uint32_t>(value));
    }
    return true;
}

bool readFloats(std::span<const std::byte> &bytes, std::span<float> values) {
    for (float &value : values) {
        const auto field = takeUInt32(bytes);
        if (!field) {
            return false;
        }
        value = std::bit_cast<float>(*field);
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

bool appendPart(std::vector<std::byte> &bytes, const StaticMeshPart &part, std::size_t texture_count) {
    if (part.vertices.size() < 3 || part.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        part.indices.size() < 3 || part.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        part.indices.size() % 3 != 0 || !appendFields(bytes, std::as_bytes(std::span(part.name))) ||
        !appendFields(bytes, std::as_bytes(std::span(part.material_name)))) {
        return false;
    }
    appendUInt32(bytes, part.material.has_value() ? 1 : 0);
    if (part.material) {
        const auto &material = *part.material;
        const std::array factors{material.metallic, material.roughness};
        if (!appendFloats(bytes, material.base_color) || !appendFloats(bytes, factors)) {
            return false;
        }
        for (const auto index : {material.base_color_texture, material.metallic_roughness_texture}) {
            if (index && *index >= texture_count) {
                return false;
            }
            appendUInt32(bytes, static_cast<std::uint32_t>(index.value_or(no_texture)));
        }
    }
    appendUInt32(bytes, static_cast<std::uint32_t>(part.vertices.size()));
    appendUInt32(bytes, static_cast<std::uint32_t>(part.indices.size()));
    for (const auto &vertex : part.vertices) {
        const std::array fields{vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x,
                                vertex.normal.y,   vertex.normal.z,   vertex.uv.x,       vertex.uv.y};
        if (!appendFloats(bytes, fields)) {
            return false;
        }
    }
    for (const auto index : part.indices) {
        if (index >= part.vertices.size()) {
            return false;
        }
        appendUInt32(bytes, index);
    }
    return true;
}

bool readPart(std::span<const std::byte> &bytes, StaticMeshPart &part, std::size_t texture_count) {
    if (!readString(bytes, part.name) || !readString(bytes, part.material_name)) {
        return false;
    }
    const auto has_material = takeUInt32(bytes);
    if (!has_material || *has_material > 1) {
        return false;
    }
    if (*has_material == 1) {
        auto &material = part.material.emplace();
        std::array<float, 2> factors{};
        if (!readFloats(bytes, material.base_color) || !readFloats(bytes, factors)) {
            return false;
        }
        material.metallic = factors[0];
        material.roughness = factors[1];
        for (auto *index : {&material.base_color_texture, &material.metallic_roughness_texture}) {
            const auto field = takeUInt32(bytes);
            if (!field || (*field != no_texture && *field >= texture_count)) {
                return false;
            }
            if (*field != no_texture) {
                *index = *field;
            }
        }
    }

    const auto vertex_count = takeUInt32(bytes);
    const auto index_count = takeUInt32(bytes);
    if (!vertex_count || !index_count || *vertex_count < 3 || *index_count < 3 || *index_count % 3 != 0 ||
        *vertex_count > bytes.size() / vertex_bytes || *vertex_count > part.vertices.max_size() ||
        *index_count > (bytes.size() - *vertex_count * vertex_bytes) / sizeof(std::uint32_t) ||
        *index_count > part.indices.max_size()) {
        return false;
    }
    part.vertices.reserve(*vertex_count);
    for (std::uint32_t index = 0; index < *vertex_count; ++index) {
        std::array<float, 8> fields{};
        if (!readFloats(bytes, fields)) {
            return false;
        }
        part.vertices.push_back(
            {{fields[0], fields[1], fields[2]}, {fields[3], fields[4], fields[5]}, {fields[6], fields[7]}});
    }
    part.indices.reserve(*index_count);
    for (std::uint32_t index = 0; index < *index_count; ++index) {
        const auto value = takeUInt32(bytes);
        if (!value || *value >= part.vertices.size()) {
            return false;
        }
        part.indices.push_back(*value);
    }
    return true;
}

} // namespace

std::expected<std::vector<std::byte>, ModelAssetError> writeModelAsset(const StaticModelAsset &model) {
    if (model.parts.empty() || model.parts.size() > std::numeric_limits<std::uint32_t>::max() ||
        model.textures.size() > no_texture) {
        return std::unexpected(ModelAssetError::InvalidModel);
    }
    std::vector<std::byte> bytes(magic.begin(), magic.end());
    appendUInt32(bytes, version);
    appendUInt32(bytes, static_cast<std::uint32_t>(model.parts.size()));
    appendUInt32(bytes, static_cast<std::uint32_t>(model.textures.size()));
    for (const auto &part : model.parts) {
        if (!appendPart(bytes, part, model.textures.size())) {
            return std::unexpected(ModelAssetError::InvalidModel);
        }
    }
    for (const auto &texture : model.textures) {
        const auto payload = writeTextureAsset(texture);
        if (!payload || !appendFields(bytes, *payload)) {
            return std::unexpected(ModelAssetError::InvalidModel);
        }
    }
    return bytes;
}

std::expected<StaticModelAsset, ModelAssetError> readModelAsset(std::span<const std::byte> bytes) {
    if (bytes.size() < magic.size() || !std::ranges::equal(bytes.first(magic.size()), magic)) {
        return std::unexpected(ModelAssetError::InvalidHeader);
    }
    bytes = bytes.subspan(magic.size());
    const auto file_version = takeUInt32(bytes);
    if (!file_version) {
        return std::unexpected(ModelAssetError::InvalidHeader);
    }
    if (*file_version != version) {
        return std::unexpected(ModelAssetError::UnsupportedVersion);
    }
    const auto parts = takeUInt32(bytes);
    const auto textures = takeUInt32(bytes);
    if (!parts || !textures || *parts == 0 || *parts > bytes.size() / min_part_bytes) {
        return std::unexpected(ModelAssetError::InvalidModel);
    }
    StaticModelAsset model;
    for (std::uint32_t index = 0; index < *parts; ++index) {
        if (!readPart(bytes, model.parts.emplace_back(), *textures)) {
            return std::unexpected(ModelAssetError::InvalidModel);
        }
    }
    for (std::uint32_t index = 0; index < *textures; ++index) {
        const auto payload = takeFields(bytes);
        if (!payload) {
            return std::unexpected(ModelAssetError::InvalidModel);
        }
        auto texture = readTextureAsset(*payload);
        if (!texture) {
            return std::unexpected(ModelAssetError::InvalidModel);
        }
        model.textures.push_back(std::move(*texture));
    }
    if (!bytes.empty()) {
        return std::unexpected(ModelAssetError::InvalidModel);
    }
    return model;
}

} // namespace star
