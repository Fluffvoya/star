#include "asset-pipeline/model_importer.hpp"
#include "asset-pipeline/texture_importer.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

namespace star::asset_pipeline {
namespace {

std::unexpected<ModelImportError> invalid(std::string message) {
    return std::unexpected(ModelImportError{std::move(message)});
}

std::expected<std::vector<std::byte>, ModelImportError> readExternal(const fastgltf::sources::URI &external,
                                                                     const GltfBufferResolver &resolver) {
    const auto path = external.uri.path();
    const auto local_path = external.uri.fspath();
    if (!resolver || !external.uri.isLocalPath() || local_path.has_root_path() || path.empty() ||
        path.find_first_of("\\:") != std::string_view::npos || path.find('\0') != std::string_view::npos ||
        std::ranges::find(local_path, "..") != local_path.end() || external.fileByteOffset != 0) {
        return invalid("Unsupported or unresolved external glTF resource");
    }
    return resolver(path);
}

std::span<const std::byte> dataBytes(const fastgltf::DataSource &source) {
    return std::visit(
        [](const auto &data) -> std::span<const std::byte> {
            if constexpr (requires { data.bytes.size(); }) {
                return {data.bytes.data(), data.bytes.size()};
            }
            return {};
        },
        source);
}

std::expected<void, ModelImportError> resolveBuffers(fastgltf::Asset &asset, const GltfBufferResolver &resolver) {
    for (auto &buffer : asset.buffers) {
        if (const auto *external = std::get_if<fastgltf::sources::URI>(&buffer.data)) {
            auto bytes = readExternal(*external, resolver);
            if (!bytes) {
                return std::unexpected(std::move(bytes.error()));
            }
            buffer.data = fastgltf::sources::Vector{std::move(*bytes)};
        }
        if (dataBytes(buffer.data).size() < buffer.byteLength) {
            return invalid("glTF buffer is shorter than its declared length");
        }
    }
    return {};
}

std::expected<ImportedTexture, ModelImportError> importImage(const fastgltf::Asset &asset, std::size_t index,
                                                             TextureUsage usage, const GltfBufferResolver &resolver) {
    const auto &image = asset.images[index];
    std::vector<std::byte> owned;
    auto bytes = dataBytes(image.data);
    if (const auto *buffer = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
        const auto &view = asset.bufferViews[buffer->bufferViewIndex];
        const auto &source = asset.buffers[view.bufferIndex];
        if (view.byteOffset > source.byteLength || view.byteLength > source.byteLength - view.byteOffset) {
            return invalid("glTF image exceeds the buffer boundaries");
        }
        bytes = dataBytes(source.data).subspan(view.byteOffset, view.byteLength);
    } else if (const auto *external = std::get_if<fastgltf::sources::URI>(&image.data)) {
        auto loaded = readExternal(*external, resolver);
        if (!loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
        owned = std::move(*loaded);
        bytes = owned;
    }
    auto texture = importTexture(bytes, {usage, true});
    if (!texture) {
        return invalid("Cannot decode glTF image " + std::to_string(index) + ": " + texture.error().message);
    }
    return std::move(*texture);
}

class MaterialTextures {
  public:
    MaterialTextures(ImportedStaticModel &model, const fastgltf::Asset &asset, const GltfBufferResolver &resolver)
        : m_model(model), m_asset(asset), m_resolve(resolver) {}

    std::expected<std::optional<std::size_t>, ModelImportError>
    import(const fastgltf::Optional<fastgltf::TextureInfo> &info, TextureUsage usage) {
        if (!info) {
            return std::nullopt;
        }
        if (info->texCoordIndex != 0) {
            return invalid("Only TEXCOORD_0 is supported for glTF material textures");
        }
        const auto &texture = m_asset.textures[info->textureIndex];
        if (!texture.imageIndex) {
            return invalid("glTF material texture is missing an image");
        }
        const auto key = std::pair{*texture.imageIndex, usage};
        if (const auto cached = m_cached.find(key); cached != m_cached.end()) {
            return cached->second;
        }
        auto imported = importImage(m_asset, key.first, usage, m_resolve);
        if (!imported) {
            return std::unexpected(std::move(imported.error()));
        }
        const auto index = m_model.textures.size();
        m_model.textures.push_back(std::move(*imported));
        m_cached.emplace(key, index);
        return index;
    }

  private:
    ImportedStaticModel &m_model;
    const fastgltf::Asset &m_asset;
    const GltfBufferResolver &m_resolve;
    std::map<std::pair<std::size_t, TextureUsage>, std::size_t> m_cached;
};

const fastgltf::Accessor *findAccessor(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive,
                                       std::string_view attribute) {
    const auto it = primitive.findAttribute(attribute);
    return it == primitive.attributes.end() ? nullptr : &asset.accessors[it->accessorIndex];
}

bool accessorFits(const fastgltf::Asset &asset, const fastgltf::Accessor &accessor, fastgltf::AccessorType type) {
    if (accessor.type != type || !accessor.bufferViewIndex || accessor.sparse || !accessor.count) {
        return false;
    }
    const auto &view = asset.bufferViews[*accessor.bufferViewIndex];
    const auto &buffer = asset.buffers[view.bufferIndex];
    const auto element_size = fastgltf::getElementByteSize(type, accessor.componentType);
    const auto stride = view.byteStride.value_or(element_size);
    return element_size != 0 && stride >= element_size && view.byteOffset <= buffer.byteLength &&
           view.byteLength <= buffer.byteLength - view.byteOffset && accessor.byteOffset <= view.byteLength &&
           element_size <= view.byteLength - accessor.byteOffset &&
           accessor.count - 1 <= (view.byteLength - accessor.byteOffset - element_size) / stride;
}

bool finite(math::Vector3 vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

std::expected<StaticMeshPart, ModelImportError>
importPrimitive(const fastgltf::Asset &asset, const fastgltf::Mesh &mesh, const fastgltf::Primitive &primitive,
                const fastgltf::math::fmat4x4 &transform, MaterialTextures *textures) {
    if (primitive.type != fastgltf::PrimitiveType::Triangles || !primitive.targets.empty()) {
        return invalid("Static glTF importer requires triangle primitives without morph targets");
    }
    const auto *positions = findAccessor(asset, primitive, "POSITION");
    const auto *normals = findAccessor(asset, primitive, "NORMAL");
    const auto *uvs = findAccessor(asset, primitive, "TEXCOORD_0");
    using fastgltf::AccessorType;
    using fastgltf::ComponentType;
    if (!positions || !accessorFits(asset, *positions, AccessorType::Vec3) ||
        positions->componentType != ComponentType::Float ||
        positions->count > std::numeric_limits<std::uint32_t>::max()) {
        return invalid("Invalid glTF position accessor");
    }
    if (normals && (!accessorFits(asset, *normals, AccessorType::Vec3) || normals->count != positions->count ||
                    normals->componentType != ComponentType::Float)) {
        return invalid("Invalid glTF normal accessor");
    }
    if (uvs && (!accessorFits(asset, *uvs, AccessorType::Vec2) || uvs->count != positions->count ||
                (uvs->componentType != ComponentType::Float && (!(uvs->componentType == ComponentType::UnsignedByte ||
                                                                  uvs->componentType == ComponentType::UnsignedShort) ||
                                                                !uvs->normalized)))) {
        return invalid("Invalid glTF texture coordinate accessor");
    }

    StaticMeshPart output;
    output.name = mesh.name;
    if (primitive.materialIndex) {
        const auto &material = asset.materials[*primitive.materialIndex];
        output.material_name = material.name;
        const auto &pbr = material.pbrData;
        const auto &base = pbr.baseColorFactor;
        auto &parameters = output.material.emplace();
        parameters.base_color = {base[0], base[1], base[2], base[3]};
        parameters.metallic = pbr.metallicFactor;
        parameters.roughness = pbr.roughnessFactor;
        if (textures) {
            if ((pbr.baseColorTexture || pbr.metallicRoughnessTexture) && !uvs) {
                return invalid("Textured glTF primitive is missing TEXCOORD_0");
            }
            auto color = textures->import(pbr.baseColorTexture, TextureUsage::Color);
            if (!color) {
                return std::unexpected(std::move(color.error()));
            }
            auto metallic_roughness = textures->import(pbr.metallicRoughnessTexture, TextureUsage::Linear);
            if (!metallic_roughness) {
                return std::unexpected(std::move(metallic_roughness.error()));
            }
            parameters.base_color_texture = *color;
            parameters.metallic_roughness_texture = *metallic_roughness;
        }
    } else {
        output.material.emplace();
    }
    output.vertices.resize(positions->count);
    if (primitive.indicesAccessor) {
        const auto &indices = asset.accessors[*primitive.indicesAccessor];
        if (!accessorFits(asset, indices, AccessorType::Scalar) || indices.normalized ||
            (indices.componentType != ComponentType::UnsignedByte &&
             indices.componentType != ComponentType::UnsignedShort &&
             indices.componentType != ComponentType::UnsignedInt)) {
            return invalid("Invalid glTF index accessor");
        }
        output.indices.resize(indices.count);
        fastgltf::copyFromAccessor<std::uint32_t>(asset, indices, output.indices.data());
    } else {
        output.indices.resize(positions->count);
        std::iota(output.indices.begin(), output.indices.end(), std::uint32_t{0});
    }
    if (output.indices.size() % 3 != 0 ||
        std::ranges::any_of(output.indices, [&](auto index) { return index >= output.vertices.size(); })) {
        return invalid("Invalid glTF triangle indices");
    }

    const math::Vector3 x{transform[0][0], transform[0][1], transform[0][2]};
    const math::Vector3 y{transform[1][0], transform[1][1], transform[1][2]};
    const math::Vector3 z{transform[2][0], transform[2][1], transform[2][2]};
    const math::Vector3 translation{transform[3][0], transform[3][1], transform[3][2]};
    const auto normal_x = math::cross(y, z);
    const auto normal_y = math::cross(z, x);
    const auto normal_z = math::cross(x, y);
    const bool mirrored = math::dot(x, normal_x) < 0.0F;

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
        asset, *positions, [&](const fastgltf::math::fvec3 &position, std::size_t index) {
            output.vertices[index].position = x * position[0] + y * position[1] + z * position[2] + translation;
        });
    if (normals) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, *normals, [&](const fastgltf::math::fvec3 &normal, std::size_t index) {
                const auto transformed = normal_x * normal[0] + normal_y * normal[1] + normal_z * normal[2];
                output.vertices[index].normal = math::normalize(mirrored ? -transformed : transformed);
            });
    }
    if (uvs) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, *uvs,
            [&](const fastgltf::math::fvec2 &uv, std::size_t index) { output.vertices[index].uv = {uv[0], uv[1]}; });
    }
    for (std::size_t index = 0; index < output.indices.size(); index += 3) {
        if (mirrored) {
            std::swap(output.indices[index + 1], output.indices[index + 2]);
        }
        if (!normals) {
            auto &a = output.vertices[output.indices[index]];
            auto &b = output.vertices[output.indices[index + 1]];
            auto &c = output.vertices[output.indices[index + 2]];
            const auto face_normal = math::cross(b.position - a.position, c.position - a.position);
            a.normal = a.normal + face_normal;
            b.normal = b.normal + face_normal;
            c.normal = c.normal + face_normal;
        }
    }
    for (auto &vertex : output.vertices) {
        if (!normals) {
            vertex.normal = math::normalize(vertex.normal);
        }
        if (!finite(vertex.position) || !finite(vertex.normal) || !std::isfinite(vertex.uv.x) ||
            !std::isfinite(vertex.uv.y)) {
            return invalid("glTF mesh contains a non-finite attribute");
        }
    }
    return output;
}

std::expected<void, ModelImportError> appendMesh(ImportedStaticModel &output, const fastgltf::Asset &asset,
                                                 const fastgltf::Mesh &mesh, const fastgltf::math::fmat4x4 &transform,
                                                 MaterialTextures *textures) {
    for (const auto &primitive : mesh.primitives) {
        auto part = importPrimitive(asset, mesh, primitive, transform, textures);
        if (!part) {
            return std::unexpected(std::move(part.error()));
        }
        output.parts.push_back(std::move(*part));
    }
    return {};
}

std::expected<ImportedStaticModel, ModelImportError>
importGltfModel(std::span<const std::byte> source, const GltfBufferResolver &resolve_buffer, bool import_textures) {
    if (source.empty()) {
        return invalid("Empty glTF source");
    }
    auto bytes = fastgltf::GltfDataBuffer::FromBytes(source.data(), source.size());
    if (bytes.error() != fastgltf::Error::None) {
        return invalid(std::string(fastgltf::getErrorMessage(bytes.error())));
    }
    fastgltf::Parser parser;
    auto parsed = parser.loadGltf(bytes.get(), {});
    if (parsed.error() != fastgltf::Error::None) {
        return invalid(std::string(fastgltf::getErrorMessage(parsed.error())));
    }
    auto &asset = parsed.get();
    if (const auto validation = fastgltf::validate(asset); validation != fastgltf::Error::None) {
        return invalid(std::string(fastgltf::getErrorMessage(validation)));
    }
    if (auto resolved = resolveBuffers(asset, resolve_buffer); !resolved) {
        return std::unexpected(std::move(resolved.error()));
    }

    ImportedStaticModel model;
    MaterialTextures material_textures(model, asset, resolve_buffer);
    auto *textures = import_textures ? &material_textures : nullptr;
    const fastgltf::math::fmat4x4 identity;
    if (asset.scenes.empty()) {
        for (const auto &mesh : asset.meshes) {
            if (auto result = appendMesh(model, asset, mesh, identity, textures); !result) {
                return std::unexpected(std::move(result.error()));
            }
        }
    } else {
        struct NodeInstance {
            std::size_t index;
            fastgltf::math::fmat4x4 parent;
        };
        std::vector<NodeInstance> pending;
        const auto scene_index = asset.defaultScene.value_or(0);
        if (scene_index >= asset.scenes.size()) {
            return invalid("Invalid glTF default scene index");
        }
        const auto &scene = asset.scenes[scene_index];
        for (auto it = scene.nodeIndices.rbegin(); it != scene.nodeIndices.rend(); ++it) {
            pending.push_back({*it, identity});
        }
        std::vector<bool> visited(asset.nodes.size());
        while (!pending.empty()) {
            const auto instance = pending.back();
            pending.pop_back();
            if (instance.index >= visited.size() || visited[instance.index]) {
                return invalid("Invalid glTF node graph");
            }
            visited[instance.index] = true;
            const auto &node = asset.nodes[instance.index];
            const auto transform = fastgltf::getTransformMatrix(node, instance.parent);
            if (node.skinIndex) {
                return invalid("Skinned glTF nodes require a non-static importer");
            }
            if (node.meshIndex) {
                if (auto result = appendMesh(model, asset, asset.meshes[*node.meshIndex], transform, textures);
                    !result) {
                    return std::unexpected(std::move(result.error()));
                }
            }
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
                pending.push_back({*it, transform});
            }
        }
    }
    if (model.parts.empty()) {
        return invalid("glTF contains no triangle geometry");
    }
    return model;
}

} // namespace

std::expected<ImportedStaticModel, ModelImportError> importGltfStaticModel(std::span<const std::byte> source,
                                                                           const GltfBufferResolver &resolve_buffer) {
    return importGltfModel(source, resolve_buffer, false);
}

std::expected<ImportedStaticModel, ModelImportError>
importGltfTexturedModel(std::span<const std::byte> source, const GltfBufferResolver &resolve_resource) {
    return importGltfModel(source, resolve_resource, true);
}

} // namespace star::asset_pipeline
