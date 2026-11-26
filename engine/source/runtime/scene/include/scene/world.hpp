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

  private:
    std::vector<std::unique_ptr<Scene>> m_scenes;
};

} // namespace star
