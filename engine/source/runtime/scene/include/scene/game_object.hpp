#pragma once

#include "core/guid.hpp"
#include "scene/export.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace star {

class Scene;
class World;
struct Transform;
struct WorldTransform;
enum class SceneError : std::uint8_t;

enum class TickGroup : std::uint8_t { PrePhysics, PostPhysics, Update, LateUpdate };

enum class EndPlayReason : std::uint8_t {
    WorldStopped,
    SceneUnloaded,
    Destroyed,
};

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
    void setName(std::string name) noexcept;
    // Valid after the object is attached to a scene.
    [[nodiscard]] Scene &getScene() const noexcept { return *m_scene; }
    [[nodiscard]] World &getWorld() const noexcept;
    [[nodiscard]] ObjectHandle getHandle() const noexcept { return m_handle; }
    [[nodiscard]] const Transform &getTransform() const noexcept;
    void setTransform(const Transform &transform) noexcept;
    [[nodiscard]] const WorldTransform &getWorldTransform() const noexcept;
    [[nodiscard]] GameObject *getParent() const noexcept;
    [[nodiscard]] std::expected<void, SceneError> setParent(GameObject *parent,
                                                            bool keep_world_transform = true) noexcept;
    [[nodiscard]] std::expected<void, SceneError> destroy() noexcept;
    void setTickEnabled(bool enabled) noexcept { m_tick_enabled = enabled; }
    void setTickGroup(TickGroup group) noexcept { m_tick_group = group; }
    [[nodiscard]] std::expected<void, SceneError> addTickPrerequisite(const GameObject &other) noexcept;

    void setActive(bool active) noexcept { m_active = active; }
    [[nodiscard]] bool isActiveInHierarchy() const noexcept { return m_active_in_hierarchy; }

    template <class Component> [[nodiscard]] Component *tryGetComponent() noexcept;
    template <class Component> [[nodiscard]] const Component *tryGetComponent() const noexcept;

    [[nodiscard]] bool isPendingDestroy() const noexcept { return m_pending_destroy; }

  protected:
    GameObject() = default;

    virtual void onCreate() noexcept {}
    virtual void onEnable() noexcept {}
    virtual void onBeginPlay() noexcept {}
    virtual void onTick(float) noexcept {}
    virtual void onEndPlay(EndPlayReason) noexcept {}
    virtual void onDisable() noexcept {}
    virtual void onDestroy() noexcept {}

  private:
    friend class Scene;
    friend struct SceneArchiveAccess;

    Guid m_guid;
    std::string m_name;
    Scene *m_scene = nullptr;
    ObjectHandle m_handle;
    std::vector<ObjectHandle> m_tick_prerequisites;
    std::uint64_t m_last_tick = 0;
    TickGroup m_tick_group = TickGroup::Update;
    bool m_tick_enabled = false;
    bool m_active = true;
    bool m_active_in_hierarchy = false;
    bool m_begun_play = false;
    bool m_pending_destroy = false;
};

} // namespace star
