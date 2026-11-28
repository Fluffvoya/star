#pragma once

#include "scene/scene.hpp"
#include "serialization/archive.hpp"

namespace star {

struct LoadedScene {
    Scene *scene;
    std::vector<serialization::ArchiveError> diagnostics;
};

struct PrefabOverride {
    Guid object;
    std::string_view path;
    serialization::ArchiveValue value;
};

struct PrefabInstance {
    ObjectHandle root;
    std::vector<serialization::ArchiveError> diagnostics;
};

[[nodiscard]] STAR_SCENE_API std::expected<std::string, serialization::ArchiveError> saveSceneYaml(const Scene &scene);
[[nodiscard]] STAR_SCENE_API std::expected<LoadedScene, serialization::ArchiveError>
loadSceneYaml(World &world, std::string_view source,
              serialization::LoadPolicy policy = serialization::LoadPolicy::Preserve,
              std::span<const reflect::TypeInfo *const> component_types = {});

[[nodiscard]] STAR_SCENE_API std::expected<std::vector<std::byte>, serialization::ArchiveError>
saveSceneBinary(const Scene &scene);
[[nodiscard]] STAR_SCENE_API std::expected<LoadedScene, serialization::ArchiveError>
loadSceneBinary(World &world, std::span<const std::byte> source,
                serialization::LoadPolicy policy = serialization::LoadPolicy::Preserve);

[[nodiscard]] STAR_SCENE_API std::expected<std::string, serialization::ArchiveError> savePrefabYaml(const Scene &scene,
                                                                                                    ObjectHandle root);

// With preserve_guids the snapshot keeps its object identities and is not marked as a prefab instance.
[[nodiscard]] STAR_SCENE_API std::expected<PrefabInstance, serialization::ArchiveError> instantiatePrefabYaml(
    Scene &scene, std::string_view source, Guid prefab_asset, std::span<const PrefabOverride> overrides = {},
    serialization::LoadPolicy policy = serialization::LoadPolicy::Preserve, bool preserve_guids = false);

} // namespace star
