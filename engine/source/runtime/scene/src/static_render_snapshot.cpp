#include "scene/static_render_snapshot.hpp"

#include "asset/asset_manager.hpp"
#include "scene/scene.hpp"

#include <algorithm>
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

struct BoundsAccumulator {
    math::Vec3 low{};
    math::Vec3 high{};
    bool started = false;

    void add(math::Vec3 position) {
        if (!started) {
            low = position;
            high = position;
            started = true;
            return;
        }
        low = {std::min(low.x, position.x), std::min(low.y, position.y), std::min(low.z, position.z)};
        high = {std::max(high.x, position.x), std::max(high.y, position.y), std::max(high.z, position.z)};
    }

    [[nodiscard]] renderer::RenderBounds result() const {
        if (!started) {
            return {};
        }
        return {(low + high) * 0.5F, math::length(high - low) * 0.5F};
    }
};

bool appendInstance(StaticRenderSnapshot &snapshot, const MeshInstance &instance, const LoadedModel &loaded,
                    BoundsAccumulator &bounds) {
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
            bounds.add(vertex.position);
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

bool readCamera(const Camera &camera, const WorldTransform &transform, renderer::RenderCamera &out) {
    const auto right = xyz(transform.matrix.columns[0]);
    const auto up = xyz(transform.matrix.columns[1]);
    const auto forward = xyz(transform.matrix.columns[2]);
    const auto eye = xyz(transform.matrix.columns[3]);
    if (!isFinite(right) || !isFinite(up) || !isFinite(forward) || !isFinite(eye) ||
        math::lengthSquared(right) <= 1e-12F || math::lengthSquared(up) <= 1e-12F ||
        math::lengthSquared(forward) <= 1e-12F || !std::isfinite(camera.vertical_fov) || camera.vertical_fov <= 0.0F ||
        camera.vertical_fov >= math::pi || !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
        camera.near_plane <= 0.0F || camera.far_plane <= camera.near_plane) {
        return false;
    }
    out.world = transform.matrix;
    out.vertical_fov = camera.vertical_fov;
    out.near_plane = camera.near_plane;
    out.far_plane = camera.far_plane;
    return true;
}

bool readLight(const Light &light, const WorldTransform &transform, renderer::RenderDirectionalLight &directional,
               renderer::RenderPointLight &point) {
    if (!isFinite(light.color) || !std::isfinite(light.intensity) || light.intensity < 0.0F ||
        !std::isfinite(light.range) || light.range <= 0.0F) {
        return false;
    }
    if (light.directional) {
        // The object looks down -Z, so the surface-to-light direction is its +Z axis.
        const auto direction = math::normalize(xyz(transform.matrix.columns[2]));
        if (!isFinite(direction) || math::lengthSquared(direction) <= 1e-12F) {
            return false;
        }
        directional = {direction, light.color, light.intensity};
        return true;
    }
    const auto position = xyz(transform.matrix.columns[3]);
    if (!isFinite(position)) {
        return false;
    }
    point = {position, light.color, light.intensity, light.range};
    return true;
}

} // namespace

std::expected<StaticRenderSnapshot, StaticRenderError> extractStaticRenderSnapshot(Scene &scene, AssetManager &assets,
                                                                                   std::uint32_t layer_mask) {
    try {
        std::vector<MeshInstance> instances;
        instances.reserve(scene.getObjectCount());
        const auto meshes = scene.each<const MeshRenderer>([&](ObjectHandle object, const MeshRenderer &mesh) noexcept {
            const auto *transform = scene.findWorldTransform(object);
            if ((mesh.layer_mask & layer_mask) != 0 && mesh.mesh.isValid() && transform &&
                !scene.findObject(object)->isPendingDestroy()) {
                instances.push_back({object, mesh.mesh, transform->matrix, mesh.cast_shadows});
            }
        });
        if (!meshes) {
            return std::unexpected(StaticRenderError{StaticRenderErrorCode::QueryFailed});
        }

        StaticRenderSnapshot snapshot;
        BoundsAccumulator bounds;
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
            if (!appendInstance(snapshot, instance, it->second, bounds)) {
                return std::unexpected(
                    StaticRenderError{StaticRenderErrorCode::InvalidTransform, instance.object, instance.mesh});
            }
        }
        snapshot.bounds = bounds.result();

        bool camera_failed = false;
        const auto cameras = scene.each<const Camera>([&](ObjectHandle object, const Camera &camera) noexcept {
            if (camera_failed || scene.findObject(object)->isPendingDestroy()) {
                return;
            }
            const auto *transform = scene.findWorldTransform(object);
            renderer::RenderCamera entry;
            if (transform == nullptr || !readCamera(camera, *transform, entry)) {
                camera_failed = true;
                return;
            }
            snapshot.cameras.push_back(entry);
        });
        if (!cameras) {
            return std::unexpected(StaticRenderError{StaticRenderErrorCode::QueryFailed});
        }
        if (camera_failed) {
            return std::unexpected(StaticRenderError{StaticRenderErrorCode::InvalidCamera});
        }

        bool light_failed = false;
        const auto lights = scene.each<const Light>([&](ObjectHandle object, const Light &light) noexcept {
            if (light_failed || scene.findObject(object)->isPendingDestroy()) {
                return;
            }
            const auto *transform = scene.findWorldTransform(object);
            renderer::RenderDirectionalLight directional;
            renderer::RenderPointLight point;
            if (transform == nullptr || !readLight(light, *transform, directional, point)) {
                light_failed = true;
                return;
            }
            if (light.directional) {
                if (!snapshot.directional_light) {
                    snapshot.directional_light = directional;
                }
            } else if (snapshot.point_lights.size() < renderer::kMaxRenderPointLights) {
                snapshot.point_lights.push_back(point);
            }
        });
        if (!lights) {
            return std::unexpected(StaticRenderError{StaticRenderErrorCode::QueryFailed});
        }
        if (light_failed) {
            return std::unexpected(StaticRenderError{StaticRenderErrorCode::InvalidLight});
        }
        return snapshot;
    } catch (...) {
        return std::unexpected(StaticRenderError{StaticRenderErrorCode::AllocationFailed});
    }
}

} // namespace star
