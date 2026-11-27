#pragma once

#include "reflection/reflection.hpp"
#include "scene/components.hpp"

#include <array>
#include <concepts>
#include <functional>
#include <span>
#include <type_traits>

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
    Busy,
    InvalidComponent,
    DuplicateComponent,
    UnknownComponent,
    RequiredComponent,
    HierarchyCycle,
    IncompatibleTransform,
    InvalidSystem,
    ScheduleCycle,
    UndeclaredAccess,
    InvalidDelta,
    TaskFailed,
};

class SpawnToken final {
  public:
    SpawnToken() = default;

  private:
    friend class Scene;
    SpawnToken(std::uint64_t scene, Guid guid) noexcept : m_scene(scene), m_guid(guid) {}
    std::uint64_t m_scene = 0;
    Guid m_guid;
};

template <class Component> [[nodiscard]] reflect::TypeId componentTypeId() noexcept {
    return reflect::makeTypeId(ComponentTraits<std::remove_cv_t<Component>>::name);
}

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

    [[nodiscard]] std::expected<SpawnToken, SceneError> queueSpawn(std::string_view name,
                                                                   std::unique_ptr<GameObject> object) noexcept;
    [[nodiscard]] std::expected<SpawnToken, SceneError> queueSpawn(std::string_view name) noexcept;
    [[nodiscard]] std::expected<SpawnToken, SceneError> queueSpawn(std::string_view name,
                                                                   reflect::TypeId game_object_type) noexcept;

    template <std::derived_from<GameObject> T>
        requires std::default_initializable<T>
    [[nodiscard]] std::expected<SpawnToken, SceneError> queueSpawn(std::string_view name) noexcept {
        try {
            return queueSpawn(name, std::make_unique<T>());
        } catch (...) {
            return std::unexpected(SceneError::CreationFailed);
        }
    }

    [[nodiscard]] ObjectHandle resolve(const SpawnToken &token) const noexcept;

    // Enqueue destruction without invalidating current borrows until commit.
    [[nodiscard]] std::expected<void, SceneError> queueDestroy(ObjectHandle handle) noexcept;
    // Calls made during a query leave the queue for the next phase boundary.
    [[nodiscard]] std::expected<void, SceneError> commitCommands() noexcept;

    [[nodiscard]] const Hierarchy *findHierarchy(ObjectHandle handle) const noexcept;
    [[nodiscard]] const WorldTransform *findWorldTransform(ObjectHandle handle) const noexcept;
    [[nodiscard]] std::expected<void, SceneError> queueSetParent(ObjectHandle child, ObjectHandle parent = {},
                                                                 bool keep_world_transform = true) noexcept;

    // Type metadata and callbacks must stay loaded until unregistration succeeds. Destruction must not throw.
    [[nodiscard]] std::expected<void, SceneError> registerComponent(const reflect::TypeInfo &type) noexcept;
    [[nodiscard]] std::expected<void, SceneError> unregisterComponents(reflect::ModuleId owner) noexcept;

    // Component pointers are borrowed until the next structural commit.
    [[nodiscard]] void *findComponent(ObjectHandle handle, reflect::TypeId type) noexcept;
    [[nodiscard]] const void *findComponent(ObjectHandle handle, reflect::TypeId type) const noexcept;
    // Adds keep an existing value and removals of missing values are harmless.
    [[nodiscard]] std::expected<void, SceneError> queueAddComponent(ObjectHandle handle, reflect::TypeId type,
                                                                    const void *initial = nullptr) noexcept;
    [[nodiscard]] std::expected<void, SceneError> queueRemoveComponent(ObjectHandle handle,
                                                                       reflect::TypeId type) noexcept;

    // Callbacks may queue structural changes. Component addresses are borrowed for the callback.
    template <class... Components, class Callback>
        requires(sizeof...(Components) > 0 && std::is_nothrow_invocable_v<Callback &, ObjectHandle, Components &...>)
    [[nodiscard]] std::expected<void, SceneError> each(Callback &&callback) noexcept {
        const std::array ids{componentTypeId<Components>()...};
        const std::array writable{!std::is_const_v<Components>...};
        auto function = std::ref(callback);
        return visitComponents(ids, writable, &function,
                               [](const void *context, ObjectHandle handle, void *const *values) noexcept {
                                   const auto &call = *static_cast<const decltype(function) *>(context);
                                   [&]<std::size_t... Index>(std::index_sequence<Index...>) {
                                       std::invoke(call, handle, *static_cast<Components *>(values[Index])...);
                                   }(std::index_sequence_for<Components...>{});
                               });
    }

    template <class Component> [[nodiscard]] Component *findComponent(ObjectHandle handle) noexcept {
        if constexpr (std::is_const_v<Component>) {
            return std::as_const(*this).findComponent<Component>(handle);
        } else {
            return static_cast<Component *>(findComponent(handle, componentTypeId<Component>()));
        }
    }

    template <class Component> [[nodiscard]] const Component *findComponent(ObjectHandle handle) const noexcept {
        return static_cast<const Component *>(findComponent(handle, componentTypeId<Component>()));
    }

  private:
    friend class World;
    friend struct SceneArchiveAccess;

    explicit Scene(World &world);
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] std::expected<ObjectHandle, SceneError> attachObject(std::unique_ptr<GameObject> object, Guid guid,
                                                                       std::string_view name) noexcept;
    std::expected<SpawnToken, SceneError> enqueueSpawn(std::string_view name, std::unique_ptr<GameObject> object,
                                                       reflect::ModuleId owner, std::string_view type) noexcept;
    void releaseModuleObjects(reflect::ModuleId owner) noexcept;
    [[nodiscard]] bool hasComponentsOwnedBy(reflect::ModuleId owner) const noexcept;
    void endPlay(EndPlayReason reason) noexcept;
    std::expected<void, SceneError> tickObjects(TickGroup group, float dt, std::uint64_t frame) noexcept;
    using ComponentVisitor = void (*)(const void *, ObjectHandle, void *const *) noexcept;
    [[nodiscard]] std::expected<void, SceneError> visitComponents(std::span<const reflect::TypeId> types,
                                                                  std::span<const bool> writable, const void *context,
                                                                  ComponentVisitor visit) noexcept;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    World &m_world;
};

template <class Component> Component *GameObject::tryGetComponent() noexcept {
    return m_scene->findComponent<Component>(m_handle);
}

template <class Component> const Component *GameObject::tryGetComponent() const noexcept {
    return std::as_const(*m_scene).findComponent<Component>(m_handle);
}

} // namespace star
