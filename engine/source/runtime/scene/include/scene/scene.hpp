#pragma once

#include "scene/game_object.hpp"

#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>

namespace star {

enum class SceneError : std::uint8_t {
    InvalidGuid,
    DuplicateGuid,
    InvalidHandle,
    ResourceExhausted,
    CreationFailed,
};

// Structural operations run on the owning thread at a phase boundary.
// Object pointers are borrowed. Keep an ObjectHandle or Guid for later use.
class STAR_SCENE_API Scene final {
  public:
    ~Scene();
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;

    [[nodiscard]] World &getWorld() const noexcept { return m_world; }
    [[nodiscard]] std::size_t getObjectCount() const noexcept;

    [[nodiscard]] std::expected<ObjectHandle, SceneError> createObject(std::string_view name) noexcept;
    [[nodiscard]] std::expected<ObjectHandle, SceneError> createObject(std::string_view name, Guid guid) noexcept;
    [[nodiscard]] GameObject *findObject(ObjectHandle handle) noexcept;
    [[nodiscard]] const GameObject *findObject(ObjectHandle handle) const noexcept;
    [[nodiscard]] GameObject *findObject(const Guid &guid) noexcept;
    [[nodiscard]] const GameObject *findObject(const Guid &guid) const noexcept;

    // Enqueue destruction without invalidating current borrows until commit.
    [[nodiscard]] std::expected<void, SceneError> queueDestroy(ObjectHandle handle) noexcept;
    void commitCommands() noexcept;

  private:
    friend class World;

    explicit Scene(World &world);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    World &m_world;
};

} // namespace star
