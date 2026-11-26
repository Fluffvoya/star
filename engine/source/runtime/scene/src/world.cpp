#include "scene/world.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace star {

World::~World() = default;

std::expected<Scene *, SceneError> World::createScene() noexcept {
    try {
        auto scene = std::unique_ptr<Scene>(new Scene(*this));
        return m_scenes.emplace_back(std::move(scene)).get();
    } catch (const std::bad_alloc &) {
        return std::unexpected(SceneError::ResourceExhausted);
    } catch (const std::length_error &) {
        return std::unexpected(SceneError::ResourceExhausted);
    } catch (...) {
        return std::unexpected(SceneError::CreationFailed);
    }
}

bool World::unloadScene(Scene &scene) noexcept {
    auto entry = std::ranges::find_if(m_scenes, [&scene](const auto &owned) { return owned.get() == &scene; });
    if (entry == m_scenes.end() || scene.isBusy()) {
        return false;
    }
    auto removed = std::move(*entry);
    m_scenes.erase(entry);
    return true;
}

} // namespace star
