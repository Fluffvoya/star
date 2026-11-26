#include "scene/world.hpp"

#include "jobs/task_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace star {
namespace {

bool overlaps(std::span<const reflect::TypeId> left, std::span<const reflect::TypeId> right) noexcept {
    return std::ranges::any_of(left, [right](auto id) { return std::ranges::find(right, id) != right.end(); });
}

bool conflicts(const SystemDescriptor &a, const SystemDescriptor &b) noexcept {
    return overlaps(a.writes, b.reads) || overlaps(a.writes, b.writes) || overlaps(b.writes, a.reads);
}

std::optional<TickGroup> objectTickGroup(TickPhase phase) noexcept {
    switch (phase) {
    case TickPhase::PrePhysics:
        return TickGroup::PrePhysics;
    case TickPhase::PostPhysics:
        return TickGroup::PostPhysics;
    case TickPhase::Update:
        return TickGroup::Update;
    case TickPhase::LateUpdate:
        return TickGroup::LateUpdate;
    default:
        return {};
    }
}

} // namespace

System::~System() = default;

SystemContext::SystemContext(World &world, const SystemDescriptor &descriptor, float delta) noexcept
    : m_world(world), m_descriptor(descriptor), m_delta(delta) {}

Scene *SystemContext::sceneAt(std::size_t index) const noexcept {
    return index < m_world.m_scenes.size() ? m_world.m_scenes[index].get() : nullptr;
}

bool SystemContext::permits(reflect::TypeId id, bool write) const noexcept {
    return std::ranges::find(m_descriptor.writes, id) != m_descriptor.writes.end() ||
           (!write && std::ranges::find(m_descriptor.reads, id) != m_descriptor.reads.end());
}

std::expected<void, SceneError> SystemContext::queueDestroy(ObjectHandle handle) noexcept {
    return defer([handle](Scene &scene) { return scene.queueDestroy(handle); });
}

std::expected<void, SceneError> SystemContext::queueSetParent(ObjectHandle child, ObjectHandle parent,
                                                              bool keep_world) noexcept {
    return defer([=](Scene &scene) { return scene.queueSetParent(child, parent, keep_world); });
}

std::expected<void, SceneError> SystemContext::queueRemoveComponent(ObjectHandle handle, reflect::TypeId id) noexcept {
    return defer([=](Scene &scene) { return scene.queueRemoveComponent(handle, id); });
}

std::expected<void, SceneError> World::registerSystem(SystemDescriptor descriptor, std::unique_ptr<System> system,
                                                      reflect::ModuleId owner) noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    if (!system || descriptor.id == 0 || descriptor.phase > TickPhase::RenderSync ||
        std::ranges::find(m_systems, descriptor.id, [](const auto &entry) { return entry.descriptor.id; }) !=
            m_systems.end()) {
        return std::unexpected(SceneError::InvalidSystem);
    }
    try {
        m_systems.push_back({std::move(descriptor), std::move(system), owner});
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

std::expected<void, SceneError> World::unregisterSystems(reflect::ModuleId owner) noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    m_dispatching = true;
    std::erase_if(m_systems, [owner](const auto &entry) { return entry.owner == owner; });
    m_dispatching = false;
    return {};
}

std::expected<void, SceneError> World::runSystems(TickPhase phase, float delta) noexcept {
    try {
        std::vector<RegisteredSystem *> remaining;
        for (auto &entry : m_systems) {
            if (entry.descriptor.phase == phase && (m_playing || entry.descriptor.run_in_editor)) {
                remaining.push_back(&entry);
            }
        }
        for (auto *entry : remaining) {
            for (const auto prerequisite : entry->descriptor.after) {
                const auto target = std::ranges::find(m_systems, prerequisite,
                                                      [](const auto &candidate) { return candidate.descriptor.id; });
                if (target == m_systems.end() || target->descriptor.phase > phase) {
                    return std::unexpected(SceneError::InvalidSystem);
                }
            }
        }
        std::vector<RegisteredSystem *> order;
        order.reserve(remaining.size());
        while (!remaining.empty()) {
            const auto ready = std::ranges::find_if(remaining, [&](const auto *entry) {
                return std::ranges::none_of(remaining, [&](const auto *pending) {
                    return std::ranges::find(entry->descriptor.after, pending->descriptor.id) !=
                           entry->descriptor.after.end();
                });
            });
            if (ready == remaining.end()) {
                return std::unexpected(SceneError::ScheduleCycle);
            }
            order.push_back(*ready);
            remaining.erase(ready);
        }

        std::vector<SystemContext> contexts;
        contexts.reserve(order.size());
        for (auto *entry : order) {
            contexts.push_back(SystemContext(*this, entry->descriptor, delta));
        }
        std::size_t first = 0;
        while (first < order.size()) {
            if (!order[first]->descriptor.parallel) {
                order[first]->system->execute(contexts[first]);
                ++first;
                continue;
            }
            TaskGraph graph;
            std::vector<TaskGraph::TaskId> tasks;
            std::size_t index = first;
            for (; index < order.size() && order[index]->descriptor.parallel; ++index) {
                const auto added = graph.addTask([&, index] {
                    order[index]->system->execute(contexts[index]);
                    return true;
                });
                if (!added) {
                    return std::unexpected(SceneError::TaskFailed);
                }
                const auto &current = order[index]->descriptor;
                for (std::size_t prior = first; prior < index; ++prior) {
                    const auto &predecessor = order[prior]->descriptor;
                    if ((conflicts(predecessor, current) ||
                         std::ranges::find(current.after, predecessor.id) != current.after.end()) &&
                        !graph.addDependency(tasks[prior - first], *added)) {
                        return std::unexpected(SceneError::TaskFailed);
                    }
                }
                tasks.push_back(*added);
            }
            if (!graph.run()) {
                return std::unexpected(SceneError::TaskFailed);
            }
            first = index;
        }

        std::expected<void, SceneError> result;
        for (auto &context : contexts) {
            for (auto &command : context.m_commands) {
                const auto applied = command.apply(*command.scene);
                if (!applied && result) {
                    result = std::unexpected(applied.error());
                }
            }
        }
        return result;
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
}

std::expected<void, SceneError> World::runPhase(TickPhase phase, float delta) noexcept {
    if (const auto group = objectTickGroup(phase); m_playing && group) {
        for (const auto &scene : m_scenes) {
            if (const auto result = scene->tickObjects(*group, delta, m_frame); !result) {
                return result;
            }
        }
    }
    auto result = runSystems(phase, delta);
    for (const auto &scene : m_scenes) {
        const auto committed = scene->commitCommands();
        if (!committed && result) {
            result = std::unexpected(committed.error());
        }
    }
    return result;
}

std::expected<void, SceneError> World::configureFixedStep(float step, std::uint32_t max_steps) noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    if (!std::isfinite(step) || step <= 0.0F || max_steps == 0) {
        return std::unexpected(SceneError::InvalidDelta);
    }
    m_fixed_step = step;
    m_max_steps = max_steps;
    m_physics_time = 0.0;
    return {};
}

std::expected<void, SceneError> World::tick(float delta) noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    if (!std::isfinite(delta) || delta < 0.0F) {
        return std::unexpected(SceneError::InvalidDelta);
    }
    m_dispatching = true;
    struct Reset {
        bool &flag;
        ~Reset() { flag = false; }
    } reset{m_dispatching};
    ++m_frame;
    constexpr auto phases = std::array{
        TickPhase::PrePhysics, TickPhase::Physics,    TickPhase::PostPhysics,          TickPhase::Update,
        TickPhase::Animation,  TickPhase::LateUpdate, TickPhase::TransformPropagation, TickPhase::RenderSync,
    };
    for (const auto phase : phases) {
        if (phase == TickPhase::Physics && m_playing) {
            const double step = m_fixed_step;
            // Preserve the fractional step and discard excess full steps after reaching the per-frame limit.
            m_physics_time += delta;
            std::uint32_t steps = 0;
            for (; steps < m_max_steps && m_physics_time >= step; ++steps) {
                m_physics_time -= step;
                if (const auto result = runPhase(phase, m_fixed_step); !result) {
                    return result;
                }
            }
            if (m_physics_time >= step) {
                m_physics_time = std::fmod(m_physics_time, step);
            }
            continue;
        }
        if (const auto result = runPhase(phase, delta); !result) {
            return result;
        }
    }
    return {};
}

} // namespace star
