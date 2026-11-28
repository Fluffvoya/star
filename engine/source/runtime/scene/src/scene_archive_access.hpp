#pragma once

#include "scene/scene_archive.hpp"

namespace star {

struct ArchivedObject {
    Guid guid;
    Guid parent;
    std::string name;
    std::uint64_t order;
    std::uint32_t sibling;
    bool active;
    serialization::ArchiveValue::Mapping fields;
    serialization::ArchiveValue::Mapping hierarchy_fields;
    std::vector<serialization::ArchiveDocument> components;
};

struct SceneArchiveAccess {
    static bool isBusy(const Scene &scene) noexcept;
    static serialization::ArchiveValue::Mapping &sceneFields(Scene &scene) noexcept;
    static const serialization::ArchiveValue::Mapping &sceneFields(const Scene &scene) noexcept;
    static std::expected<std::vector<ArchivedObject>, serialization::ArchiveError>
    capture(const Scene &scene, ObjectHandle root,
            serialization::CapturePolicy policy = serialization::CapturePolicy::OmitDefaults);
    static std::expected<void, serialization::ArchiveError>
    restore(Scene &scene, std::span<ArchivedObject> objects, serialization::LoadPolicy policy,
            std::vector<serialization::ArchiveError> &diagnostics,
            SceneSnapshotRestore mode = SceneSnapshotRestore::Play);
};

} // namespace star
