#pragma once

#include "scene/scene.hpp"

#include <memory>
#include <vector>

namespace star {

// A World owns its scenes and keeps their addresses stable until unloaded.
class STAR_SCENE_API World final {
  public:
    World() = default;
    ~World();
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    [[nodiscard]] std::expected<Scene *, SceneError> createScene() noexcept;
    bool unloadScene(Scene &scene) noexcept;
    [[nodiscard]] bool isPlaying() const noexcept { return m_playing; }
    [[nodiscard]] std::expected<void, SceneError> beginPlay() noexcept;
    [[nodiscard]] std::expected<void, SceneError> endPlay() noexcept;

  private:
    [[nodiscard]] bool isBusy() const noexcept;

    std::vector<std::unique_ptr<Scene>> m_scenes;
    bool m_playing = false;
    bool m_dispatching = false;
};

} // namespace star
