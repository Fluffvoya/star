#include "scene/world.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace star {

World::~World() {
    m_dispatching = true;
    m_scenes.clear();
}

bool World::isBusy() const noexcept {
    return m_dispatching || std::ranges::any_of(m_scenes, [](const auto &scene) { return scene->isBusy(); });
}

std::expected<void, SceneError> World::beginPlay() noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    if (std::exchange(m_playing, true)) {
        return {};
    }
    std::expected<void, SceneError> result;
    m_dispatching = true;
    for (const auto &scene : m_scenes) {
        const auto committed = scene->commitCommands();
        if (!committed && result) {
            result = std::unexpected(committed.error());
        }
    }
    m_dispatching = false;
    return result;
}

std::expected<void, SceneError> World::endPlay() noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    if (!std::exchange(m_playing, false)) {
        return {};
    }
    m_physics_time = 0.0;
    m_dispatching = true;
    for (const auto &scene : m_scenes) {
        scene->endPlay(EndPlayReason::WorldStopped);
    }
    m_dispatching = false;
    return {};
}

std::expected<Scene *, SceneError> World::createScene() noexcept {
    if (m_dispatching) {
        return std::unexpected(SceneError::Busy);
    }
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
    if (m_dispatching || entry == m_scenes.end() || scene.isBusy()) {
        return false;
    }
    auto removed = std::move(*entry);
    m_scenes.erase(entry);
    return true;
}

} // namespace star
