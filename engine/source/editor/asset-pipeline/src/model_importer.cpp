#include "asset-pipeline/model_importer.hpp"

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace star::asset_pipeline {
namespace {

math::Vector3 toVector(ufbx_vec3 value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

std::string copyString(ufbx_string text) {
    return text.length == 0 ? std::string{} : std::string(text.data, text.length);
}

bool isFinite(const StaticMeshVertex &vertex) {
    return std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z) &&
           std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z) &&
           std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
}

std::expected<StaticMeshPart, ModelImportError> importPart(const ufbx_node &node, const ufbx_mesh_part &part,
                                                           std::span<std::uint32_t> triangles) {
    const auto &mesh = *node.mesh;
    const auto normal_matrix = ufbx_matrix_for_normals(&node.geometry_to_world);
    const bool mirrored = ufbx_matrix_determinant(&node.geometry_to_world) < 0;
    StaticMeshPart output;
    output.name = copyString(node.name);
    if (part.index < node.materials.count) {
        output.material_name = copyString(node.materials.data[part.index]->name);
    }
    if (part.num_triangles > std::numeric_limits<std::uint32_t>::max() / 3 ||
        part.num_triangles > output.vertices.max_size() / 3) {
        return std::unexpected(ModelImportError{"Mesh part exceeds index capacity"});
    }
    output.vertices.reserve(part.num_triangles * 3);
    for (const auto face_index : part.face_indices) {
        const auto face = mesh.faces.data[face_index];
        if (face.num_indices < 3) {
            continue;
        }
        const auto count = ufbx_triangulate_face(triangles.data(), triangles.size(), &mesh, face);
        if (count == 0) {
            return std::unexpected(ModelImportError{"Cannot triangulate mesh face"});
        }
        for (std::size_t triangle = 0; triangle < count; ++triangle) {
            auto corners = triangles.subspan(triangle * 3, 3);
            if (mirrored) {
                std::swap(corners[1], corners[2]);
            }
            for (const auto corner : corners) {
                const auto position = ufbx_get_vertex_vec3(&mesh.vertex_position, corner);
                const auto normal = ufbx_get_vertex_vec3(&mesh.vertex_normal, corner);
                const auto world_normal = ufbx_transform_direction(&normal_matrix, normal);
                StaticMeshVertex vertex;
                vertex.position = toVector(ufbx_transform_position(&node.geometry_to_world, position));
                vertex.normal = toVector(ufbx_vec3_normalize(world_normal));
                if (mirrored) {
                    vertex.normal = -vertex.normal;
                }
                if (mesh.vertex_uv.exists) {
                    const auto uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, corner);
                    vertex.uv = {static_cast<float>(uv.x), 1.0F - static_cast<float>(uv.y)};
                }
                if (!isFinite(vertex)) {
                    return std::unexpected(ModelImportError{"Mesh contains a non-finite attribute"});
                }
                output.vertices.push_back(vertex);
            }
        }
    }

    static_assert(sizeof(StaticMeshVertex) == 8 * sizeof(float));
    output.indices.resize(output.vertices.size());
    ufbx_vertex_stream stream{output.vertices.data(), output.vertices.size(), sizeof(StaticMeshVertex)};
    const auto unique =
        ufbx_generate_indices(&stream, 1, output.indices.data(), output.indices.size(), nullptr, nullptr);
    if (unique == 0) {
        return std::unexpected(ModelImportError{"Cannot index mesh vertices"});
    }
    output.vertices.resize(unique);
    return output;
}

} // namespace

std::expected<ImportedStaticModel, ModelImportError> importStaticModel(std::span<const std::byte> source,
                                                                       StaticModelFormat format) {
    if (source.empty()) {
        return std::unexpected(ModelImportError{"Empty model source"});
    }
    ufbx_load_opts options{};
    options.file_format = format == StaticModelFormat::Fbx ? UFBX_FILE_FORMAT_FBX : UFBX_FILE_FORMAT_OBJ;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.generate_missing_normals = true;

    ufbx_error error{};
    using Scene = std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)>;
    Scene scene(ufbx_load_memory(source.data(), source.size(), &options, &error), ufbx_free_scene);
    if (!scene) {
        std::array<char, 1024> message{};
        ufbx_format_error(message.data(), message.size(), &error);
        return std::unexpected(ModelImportError{message.data()});
    }

    ImportedStaticModel model;
    for (const auto *node : scene->nodes) {
        const auto *mesh = node->mesh;
        if (!mesh || mesh->num_triangles == 0) {
            continue;
        }
        if (mesh->skin_deformers.count != 0 || mesh->blend_deformers.count != 0 || mesh->cache_deformers.count != 0) {
            return std::unexpected(ModelImportError{"Mesh deformers require a non-static importer"});
        }
        if (!mesh->vertex_position.exists || !mesh->vertex_normal.exists) {
            return std::unexpected(ModelImportError{"Mesh is missing positions or normals"});
        }
        if (mesh->max_face_triangles > std::vector<std::uint32_t>().max_size() / 3) {
            return std::unexpected(ModelImportError{"Face exceeds triangulation capacity"});
        }
        std::vector<std::uint32_t> triangles(mesh->max_face_triangles * 3);
        for (const auto &part : mesh->material_parts) {
            if (part.num_triangles == 0) {
                continue;
            }
            auto imported = importPart(*node, part, triangles);
            if (!imported) {
                return std::unexpected(std::move(imported.error()));
            }
            model.parts.push_back(std::move(*imported));
        }
    }
    if (model.parts.empty()) {
        return std::unexpected(ModelImportError{"Model contains no triangle geometry"});
    }
    return model;
}

} // namespace star::asset_pipeline
