#pragma once

#include "scene/scene.hpp"

#include <vector>

namespace star {

enum class TickPhase : std::uint8_t {
    PrePhysics,
    Physics,
    PostPhysics,
    Update,
    Animation,
    LateUpdate,
    TransformPropagation,
    RenderSync,
};

struct SystemDescriptor {
    reflect::TypeId id = 0;
    TickPhase phase = TickPhase::Update;
    std::vector<reflect::TypeId> reads;
    std::vector<reflect::TypeId> writes;
    std::vector<reflect::TypeId> after;
    bool parallel = false;
    bool run_in_editor = false;
};

class SystemContext;

class STAR_SCENE_API System {
  public:
    virtual ~System();
    virtual void execute(SystemContext &context) noexcept = 0;
};

class STAR_SCENE_API SystemContext final {
  public:
    [[nodiscard]] float getDeltaTime() const noexcept { return m_delta; }
    [[nodiscard]] TickPhase getPhase() const noexcept { return m_descriptor.phase; }

    template <class... Components, class Callback>
        requires(sizeof...(Components) > 0 && std::is_nothrow_invocable_v<Callback &, ObjectHandle, Components &...>)
    [[nodiscard]] std::expected<void, SceneError> each(Callback &&callback) noexcept {
        if (!(permits(componentTypeId<Components>(), !std::is_const_v<Components>) && ...)) {
            return std::unexpected(SceneError::UndeclaredAccess);
        }
        auto *previous = m_scene;
        for (std::size_t index = 0; auto *scene = sceneAt(index); ++index) {
            m_scene = scene;
            const auto result = scene->each<Components...>([&](ObjectHandle handle, Components &...values) noexcept {
                const auto *object = std::as_const(*scene).findObject(handle);
                if (object->isActiveInHierarchy() && !object->isPendingDestroy()) {
                    std::invoke(callback, handle, values...);
                }
            });
            if (!result) {
                m_scene = previous;
                return result;
            }
        }
        m_scene = previous;
        return {};
    }

    [[nodiscard]] std::expected<void, SceneError> queueDestroy(ObjectHandle handle) noexcept;
    [[nodiscard]] std::expected<void, SceneError> queueSetParent(ObjectHandle child, ObjectHandle parent,
                                                                 bool keep_world = true) noexcept;
    [[nodiscard]] std::expected<void, SceneError> queueRemoveComponent(ObjectHandle handle,
                                                                       reflect::TypeId id) noexcept;

    template <class T>
    [[nodiscard]] std::expected<void, SceneError> queueAddComponent(ObjectHandle handle, const T &initial) noexcept {
        try {
            return defer([handle, value = initial](Scene &scene) {
                return scene.queueAddComponent(handle, componentTypeId<T>(), &value);
            });
        } catch (...) {
            return std::unexpected(SceneError::ResourceExhausted);
        }
    }

  private:
    friend class World;
    using Command = std::move_only_function<std::expected<void, SceneError>(Scene &)>;
    struct PendingCommand {
        Scene *scene;
        Command apply;
    };
    SystemContext(World &world, const SystemDescriptor &descriptor, float delta) noexcept;

    template <class F> std::expected<void, SceneError> defer(F &&command) noexcept {
        if (m_scene == nullptr) {
            return std::unexpected(SceneError::InvalidHandle);
        }
        try {
            m_commands.push_back({m_scene, Command(std::forward<F>(command))});
        } catch (...) {
            return std::unexpected(SceneError::ResourceExhausted);
        }
        return {};
    }

    [[nodiscard]] bool permits(reflect::TypeId id, bool write) const noexcept;
    [[nodiscard]] Scene *sceneAt(std::size_t index) const noexcept;
    World &m_world;
    const SystemDescriptor &m_descriptor;
    float m_delta;
    Scene *m_scene = nullptr;
    std::vector<PendingCommand> m_commands;
};

} // namespace star
