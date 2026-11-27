#pragma once

#include "scene/game_module.hpp"

#include <memory>
#include <vector>

namespace star {

// A World owns its scenes and keeps their addresses stable until unloaded.
class STAR_SCENE_API World final {
  public:
    World();
    ~World();
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    [[nodiscard]] std::expected<Scene *, SceneError> createScene() noexcept;
    bool unloadScene(Scene &scene) noexcept;
    [[nodiscard]] bool isPlaying() const noexcept { return m_playing; }
    [[nodiscard]] std::expected<void, SceneError> beginPlay() noexcept;
    [[nodiscard]] std::expected<void, SceneError> endPlay() noexcept;
    [[nodiscard]] std::expected<void, SceneError>
    registerSystem(SystemDescriptor descriptor, std::unique_ptr<System> system,
                   reflect::ModuleId owner = reflect::builtin_module) noexcept;
    [[nodiscard]] std::expected<void, SceneError> unregisterSystems(reflect::ModuleId owner) noexcept;
    [[nodiscard]] std::expected<void, SceneError> tick(float delta) noexcept;
    [[nodiscard]] std::expected<reflect::ModuleId, GameModuleError>
    loadGameModule(const std::filesystem::path &path) noexcept;
    [[nodiscard]] std::expected<void, GameModuleError> unloadGameModule(reflect::ModuleId owner) noexcept;
    [[nodiscard]] std::expected<void, SceneError> configureFixedStep(float step, std::uint32_t max_steps) noexcept;

  private:
    friend class SystemContext;
    friend class Scene;
    friend struct SceneArchiveAccess;
    struct LoadedGameModule;

    struct RegisteredObject {
        const GameObjectType *type = nullptr;
        reflect::ModuleId owner = reflect::builtin_module;
    };
    [[nodiscard]] RegisteredObject findGameObjectType(reflect::TypeId id) const noexcept;
    [[nodiscard]] bool installGameComponents(Scene &scene) const noexcept;
    void releaseGameModule(LoadedGameModule &module) noexcept;

    struct RegisteredSystem {
        SystemDescriptor descriptor;
        std::unique_ptr<System> system;
        reflect::ModuleId owner;
    };
    std::expected<void, SceneError> runSystems(TickPhase phase, float delta) noexcept;
    std::expected<void, SceneError> runPhase(TickPhase phase, float delta) noexcept;
    [[nodiscard]] bool isBusy() const noexcept;

    std::vector<std::unique_ptr<LoadedGameModule>> m_game_modules;
    reflect::TypeRegistry m_module_reflection;
    bool m_loading_module = false;
    std::vector<std::unique_ptr<Scene>> m_scenes;
    std::vector<RegisteredSystem> m_systems;
    float m_fixed_step = 1.0F / 60.0F;
    double m_physics_time = 0.0;
    std::uint32_t m_max_steps = 4;
    std::uint64_t m_frame = 0;
    bool m_playing = false;
    bool m_dispatching = false;
};

} // namespace star
