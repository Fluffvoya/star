#include "scene/static_render_snapshot.hpp"

#include "asset/asset_manager.hpp"
#include "scene/scene.hpp"

#include <cmath>
#include <map>
#include <utility>

namespace star {
namespace {

struct MeshInstance {
    ObjectHandle object;
    Guid mesh;
    math::Mat4 transform;
    bool cast_shadows;
};

struct LoadedModel {
    AssetHandle<StaticModelAsset> handle;
    std::size_t texture_offset;
};

math::Vec3 xyz(math::Mat4::Vector4 column) { return {column.x, column.y, column.z}; }

bool isFinite(math::Vec3 vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

bool appendInstance(StaticRenderSnapshot &snapshot, const MeshInstance &instance, const LoadedModel &loaded) {
    const auto &matrix = instance.transform;
    const auto x = xyz(matrix.columns[0]);
    const auto y = xyz(matrix.columns[1]);
    const auto z = xyz(matrix.columns[2]);
    const auto normal_x = math::cross(y, z);
    const auto normal_y = math::cross(z, x);
    const auto normal_z = math::cross(x, y);
    const auto determinant = math::dot(x, normal_x);
    if (!std::isfinite(determinant) || determinant == 0.0F) {
        return false;
    }

    for (auto part : loaded.handle.get()->parts) {
        for (auto &vertex : part.vertices) {
            vertex.position = matrix.transformPoint(vertex.position);
            auto normal = normal_x * vertex.normal.x + normal_y * vertex.normal.y + normal_z * vertex.normal.z;
            if (determinant < 0.0F) {
                normal = -normal;
            }
            const auto magnitude = std::hypot(normal.x, normal.y, normal.z);
            if (!isFinite(vertex.position) || !std::isfinite(magnitude)) {
                return false;
            }
            vertex.normal = magnitude == 0.0F ? math::Vec3{} : normal / magnitude;
        }
        if (determinant < 0.0F) {
            for (std::size_t index = 0; index < part.indices.size(); index += 3) {
                std::swap(part.indices[index + 1], part.indices[index + 2]);
            }
        }
        if (part.material) {
            for (auto *index : {&part.material->base_color_texture, &part.material->metallic_roughness_texture}) {
                if (*index) {
                    **index += loaded.texture_offset;
                }
            }
        }
        snapshot.model.parts.push_back(std::move(part));
        snapshot.shadow_casters.push_back(instance.cast_shadows);
    }
    return true;
}

} // namespace

std::expected<StaticRenderSnapshot, StaticRenderError> extractStaticRenderSnapshot(Scene &scene, AssetManager &assets,
                                                                                   std::uint32_t layer_mask) {
    try {
        std::vector<MeshInstance> instances;
        instances.reserve(scene.getObjectCount());
        const auto queried =
            scene.each<const MeshRenderer>([&](ObjectHandle object, const MeshRenderer &mesh) noexcept {
                const auto *transform = scene.findWorldTransform(object);
                if ((mesh.layer_mask & layer_mask) != 0 && mesh.mesh.isValid() && transform &&
                    !scene.findObject(object)->isPendingDestroy()) {
                    instances.push_back({object, mesh.mesh, transform->matrix, mesh.cast_shadows});
                }
            });
        if (!queried) {
            return std::unexpected(StaticRenderError{StaticRenderErrorCode::QueryFailed});
        }

        StaticRenderSnapshot snapshot;
        std::map<Guid, LoadedModel> loaded_models;
        for (const auto &instance : instances) {
            auto it = loaded_models.find(instance.mesh);
            if (it == loaded_models.end()) {
                auto loaded = AssetRef<StaticModelAsset>{instance.mesh}.load(assets);
                if (!loaded) {
                    return std::unexpected(
                        StaticRenderError{StaticRenderErrorCode::AssetLoadFailed, instance.object, instance.mesh});
                }
                const auto &textures = loaded->get()->textures;
                it = loaded_models
                         .emplace(instance.mesh, LoadedModel{std::move(*loaded), snapshot.model.textures.size()})
                         .first;
                snapshot.model.textures.insert(snapshot.model.textures.end(), textures.begin(), textures.end());
            }
            if (!appendInstance(snapshot, instance, it->second)) {
                return std::unexpected(
                    StaticRenderError{StaticRenderErrorCode::InvalidTransform, instance.object, instance.mesh});
            }
        }
        return snapshot;
    } catch (...) {
        return std::unexpected(StaticRenderError{StaticRenderErrorCode::AllocationFailed});
    }
}

} // namespace star
