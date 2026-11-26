#pragma once

#include "core/guid.hpp"
#include "scene/export.hpp"

#include <cstdint>
#include <string>

namespace star {

class Scene;
class World;

class ObjectHandle final {
  public:
    constexpr ObjectHandle() = default;
    friend constexpr bool operator==(ObjectHandle, ObjectHandle) = default;

  private:
    friend class Scene;

    constexpr ObjectHandle(std::uint64_t scene, std::uint32_t index, std::uint32_t generation) noexcept
        : m_scene(scene), m_index(index), m_generation(generation) {}

    std::uint64_t m_scene = 0;
    std::uint32_t m_index = 0;
    std::uint32_t m_generation = 0;
};

class STAR_SCENE_API GameObject {
  public:
    virtual ~GameObject();
    GameObject(const GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;

    [[nodiscard]] const Guid &getGuid() const noexcept { return m_guid; }
    [[nodiscard]] const std::string &getName() const noexcept { return m_name; }
    // Valid after the object is attached to a scene.
    [[nodiscard]] Scene &getScene() const noexcept { return *m_scene; }
    [[nodiscard]] World &getWorld() const noexcept;
    [[nodiscard]] ObjectHandle getHandle() const noexcept { return m_handle; }
    [[nodiscard]] bool isPendingDestroy() const noexcept { return m_pending_destroy; }

  protected:
    GameObject() = default;

  private:
    friend class Scene;

    Guid m_guid;
    std::string m_name;
    Scene *m_scene = nullptr;
    ObjectHandle m_handle;
    bool m_pending_destroy = false;
};

} // namespace star
