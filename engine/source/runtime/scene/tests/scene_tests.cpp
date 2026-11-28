#include "test_support.hpp"

#include "asset/asset_manager.hpp"
#include "scene/scene_archive.hpp"
#include "scene/static_render_snapshot.hpp"
#include "scene/world.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>

namespace {

STAR_TEST(sceneCreatesObjectsAndResolvesTheirIdentity) {
    star::World world;
    auto created = world.createScene();
    STAR_CHECK(created.has_value());
    star::Scene &scene = **created;
    const auto handle = scene.createObject("Player");
    STAR_CHECK(handle.has_value());
    const auto *object = scene.findObject(*handle);
    STAR_CHECK(object != nullptr);
    STAR_CHECK(object->getGuid().isValid());
    STAR_CHECK(object->getHandle() == *handle);
    STAR_CHECK(object->getName() == "Player");
    STAR_CHECK(&object->getScene() == &scene);
    STAR_CHECK(&object->getWorld() == &world);
    STAR_CHECK(scene.findObject(object->getGuid()) == object);
    STAR_CHECK(scene.getObjectCount() == 1);
    const auto duplicate = scene.createObject("Duplicate", object->getGuid());
    STAR_CHECK(!duplicate && duplicate.error() == star::SceneError::DuplicateGuid);
    const auto invalid = scene.createObject("Invalid", {});
    STAR_CHECK(!invalid && invalid.error() == star::SceneError::InvalidGuid);
    STAR_CHECK(scene.getObjectCount() == 1);
}

STAR_TEST(sceneInvalidatesDeletedHandlesOnlyAtCommit) {
    star::World world;
    auto created = world.createScene();
    STAR_CHECK(created.has_value());
    star::Scene &scene = **created;
    const auto first = scene.createObject("First");
    const auto second = scene.createObject("Second");
    STAR_CHECK(first && second);
    const auto guid = scene.findObject(*first)->getGuid();
    STAR_CHECK(scene.queueDestroy(*first).has_value());
    STAR_CHECK(scene.queueDestroy(*first).has_value());
    STAR_CHECK(scene.findObject(*first)->isPendingDestroy());
    STAR_CHECK(scene.findObject(guid) == scene.findObject(*first));
    STAR_CHECK(scene.getObjectCount() == 2);

    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.findObject(*first) == nullptr);
    STAR_CHECK(scene.findObject(guid) == nullptr);
    STAR_CHECK(scene.findObject(*second) != nullptr);
    STAR_CHECK(scene.getObjectCount() == 1);
    const auto stale = scene.queueDestroy(*first);
    STAR_CHECK(!stale && stale.error() == star::SceneError::InvalidHandle);
    const auto restored = scene.createObject("Restored", guid);
    STAR_CHECK(restored && *restored != *first);
    STAR_CHECK(scene.findObject(*first) == nullptr);
    STAR_CHECK(scene.findObject(guid) == scene.findObject(*restored));
}

STAR_TEST(sceneRejectsHandlesFromOtherAndUnloadedScenes) {
    star::World world;
    star::World other_world;
    auto first_scene = world.createScene();
    auto second_scene = other_world.createScene();
    STAR_CHECK(first_scene && second_scene);
    const auto first = (*first_scene)->createObject("First");
    const auto second = (*second_scene)->createObject("Second");
    STAR_CHECK(first && second);
    STAR_CHECK(!world.unloadScene(**second_scene));
    STAR_CHECK((*second_scene)->findObject(*first) == nullptr);
    STAR_CHECK((*first_scene)->findObject(*second) == nullptr);
    STAR_CHECK((*first_scene)->findObject(star::ObjectHandle{}) == nullptr);
    STAR_CHECK(world.unloadScene(**first_scene));
    auto replacement = world.createScene();
    STAR_CHECK(replacement.has_value());
    STAR_CHECK((*replacement)->createObject("Replacement").has_value());
    STAR_CHECK((*replacement)->findObject(*first) == nullptr);
}

} // namespace

namespace scene_test {

struct alignas(64) Velocity {
    int value = 1;
};

struct Position {
    int value = 2;
};

struct Counted {
    static inline int count = 0;
    Counted() noexcept { ++count; }
    Counted(const Counted &) noexcept { ++count; }
    ~Counted() { --count; }
};

} // namespace scene_test

template <> struct star::ComponentTraits<scene_test::Velocity> {
    static constexpr auto name = "scene_test::Velocity";
};
template <> struct star::ComponentTraits<scene_test::Position> {
    static constexpr auto name = "scene_test::Position";
};
template <> struct star::ComponentTraits<scene_test::Counted> {
    static constexpr auto name = "scene_test::Counted";
};

namespace {

template <class T> star::reflect::TypeInfo componentInfo(star::reflect::ModuleId owner) {
    return {star::componentTypeId<T>(),
            star::ComponentTraits<T>::name,
            star::reflect::TypeKind::Struct,
            sizeof(T),
            alignof(T),
            {},
            star::reflect::makeTypeOps<T>(),
            owner,
            1,
            nullptr};
}

STAR_TEST(sceneQueriesRegisteredComponentsAndDefersStructuralChanges) {
    using namespace scene_test;
    const auto velocity = componentInfo<Velocity>(1);
    const auto position = componentInfo<Position>(1);
    star::World world;
    auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    STAR_CHECK(scene.registerComponent(velocity));
    STAR_CHECK(scene.registerComponent(position));
    const auto duplicate = scene.registerComponent(velocity);
    STAR_CHECK(!duplicate && duplicate.error() == star::SceneError::DuplicateComponent);
    auto malformed = velocity;
    malformed.alignment = 3;
    const auto invalid = scene.registerComponent(malformed);
    STAR_CHECK(!invalid && invalid.error() == star::SceneError::InvalidComponent);

    const auto first = scene.createObject("First");
    const auto second = scene.createObject("Second");
    STAR_CHECK(first && second);
    const Velocity initial{7};
    STAR_CHECK(scene.queueAddComponent(*first, velocity.id, &initial));
    STAR_CHECK(scene.queueAddComponent(*first, position.id));
    STAR_CHECK(scene.queueAddComponent(*second, velocity.id));
    STAR_CHECK(scene.findComponent<Position>(*first) == nullptr);
    STAR_CHECK(scene.commitCommands());
    const auto *value = scene.findComponent<Velocity>(*first);
    STAR_CHECK(value != nullptr && value->value == 7);
    STAR_CHECK(reinterpret_cast<std::uintptr_t>(value) % alignof(Velocity) == 0);

    int visits = 0;
    bool was_deferred = false;
    bool was_protected = false;
    const auto queried = scene.each<const Velocity, Position>(
        [&](star::ObjectHandle handle, const Velocity &speed, Position &location) noexcept {
            ++visits;
            location.value += speed.value;
            const auto removed = scene.queueRemoveComponent(handle, velocity.id);
            const auto committed = scene.commitCommands();
            was_deferred = removed.has_value() && !committed && committed.error() == star::SceneError::Busy &&
                           scene.findComponent<Velocity>(handle) == &speed;
            const auto blocked = scene.createObject("Invalid");
            was_protected = !blocked && blocked.error() == star::SceneError::Busy && !world.unloadScene(scene);
        });
    STAR_CHECK(queried && visits == 1);
    STAR_CHECK(was_deferred && was_protected);
    STAR_CHECK(scene.findComponent<Position>(*first)->value == 9);
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.findComponent<Velocity>(*first) == nullptr);
    STAR_CHECK(scene.findComponent<Velocity>(*second) != nullptr);
}

STAR_TEST(sceneUnregisterDestroysLiveAndQueuedComponentValues) {
    using scene_test::Counted;
    const auto component = componentInfo<Counted>(23);
    star::World world;
    auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    STAR_CHECK(scene.registerComponent(component));
    const auto object = scene.createObject("Object");
    STAR_CHECK(object);
    STAR_CHECK(scene.queueAddComponent(*object, component.id));
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.queueAddComponent(*object, component.id));
    STAR_CHECK(Counted::count == 2);
    STAR_CHECK(scene.unregisterComponents(component.owner));
    STAR_CHECK(Counted::count == 0);
    STAR_CHECK(scene.findComponent<Counted>(*object) == nullptr);
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.registerComponent(component));
    STAR_CHECK(scene.queueAddComponent(*object, component.id));
    STAR_CHECK(scene.queueDestroy(*object));
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(Counted::count == 0);
}

} // namespace

namespace {

STAR_TEST(scenePropagatesTransformsAndRejectsPendingCycles) {
    star::World world;
    auto created = world.createScene();
    auto other = world.createScene();
    STAR_CHECK(created && other);
    auto &scene = **created;
    const auto root = scene.createObject("Root");
    const auto child = scene.createObject("Child");
    const auto leaf = scene.createObject("Leaf");
    const auto sibling = scene.createObject("Sibling");
    const auto foreign = (*other)->createObject("Foreign");
    STAR_CHECK(root && child && leaf && sibling && foreign);

    scene.findComponent<star::Transform>(*root)->position = {10.0F, 0.0F, 0.0F};
    scene.findComponent<star::Transform>(*child)->position = {2.0F, 0.0F, 0.0F};
    STAR_CHECK(scene.queueSetParent(*child, *root, false));
    STAR_CHECK(scene.queueSetParent(*leaf, *child, false));
    STAR_CHECK(scene.queueSetParent(*sibling, *root, false));
    STAR_CHECK(scene.findHierarchy(*child)->parent == star::ObjectHandle{});
    const auto cyclic = scene.queueSetParent(*root, *leaf, false);
    STAR_CHECK(!cyclic && cyclic.error() == star::SceneError::HierarchyCycle);
    const auto cross_scene = scene.queueSetParent(*root, *foreign, false);
    STAR_CHECK(!cross_scene && cross_scene.error() == star::SceneError::InvalidHandle);
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.findHierarchy(*root)->child_count == 2);
    STAR_CHECK(scene.findHierarchy(*child)->next_sibling == *sibling);
    STAR_CHECK(scene.findHierarchy(*sibling)->sibling_index == 1);
    STAR_CHECK(scene.findHierarchy(*leaf)->depth == 2);
    STAR_CHECK(scene.findWorldTransform(*leaf)->matrix.transformPoint({}).x == 12.0F);

    const auto changed = scene.each<star::Transform>([&](star::ObjectHandle handle, star::Transform &local) noexcept {
        if (handle == *root) {
            local.position.x = 20.0F;
        }
    });
    STAR_CHECK(changed);
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.findWorldTransform(*leaf)->matrix.transformPoint({}).x == 22.0F);
    STAR_CHECK(scene.queueDestroy(*child));
    STAR_CHECK(scene.findObject(*leaf)->isPendingDestroy());
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.findObject(*child) == nullptr && scene.findObject(*leaf) == nullptr);
    STAR_CHECK(scene.findHierarchy(*root)->first_child == *sibling);
    STAR_CHECK(scene.findHierarchy(*sibling)->sibling_index == 0);
}

STAR_TEST(sceneReparentsAcrossRotatedScaledTransformsAndReportsSingularParents) {
    star::World world;
    auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    const auto parent = scene.createObject("Parent");
    const auto child = scene.createObject("Child");
    STAR_CHECK(parent && child);
    auto *parent_object = scene.findObject(*parent);
    auto *child_object = scene.findObject(*child);
    parent_object->setTransform({{3.0F, 4.0F, 0.0F}, star::math::Quat::fromAxisAngle({0, 0, 1}, 0.7F), {2, 2, 2}});
    child_object->setTransform({{8.0F, 3.0F, 1.0F}, star::math::Quat::fromAxisAngle({1, 0, 0}, 1.0F), {-1, 2, 3}});
    STAR_CHECK(scene.commitCommands());
    const auto point = star::math::Vec3{0.5F, 0.5F, 0.5F};
    const auto before = child_object->getWorldTransform().matrix.transformPoint(point);
    STAR_CHECK(child_object->setParent(parent_object));
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(child_object->getParent() == parent_object);
    STAR_CHECK(star::math::length(child_object->getWorldTransform().matrix.transformPoint(point) - before) < 1e-4F);
    STAR_CHECK(child_object->setParent(nullptr));
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(child_object->getParent() == nullptr);
    STAR_CHECK(star::math::length(child_object->getWorldTransform().matrix.transformPoint(point) - before) < 1e-4F);

    scene.findComponent<star::Transform>(*parent)->scale.x = 0.0F;
    STAR_CHECK(child_object->setParent(parent_object));
    const auto incompatible = scene.commitCommands();
    STAR_CHECK(!incompatible && incompatible.error() == star::SceneError::IncompatibleTransform);
    STAR_CHECK(child_object->getParent() == nullptr);
}

STAR_TEST(sceneRegistersOptionalBuiltinsAndKeepsRequiredLocalTransform) {
    star::World world;
    auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    const auto object = scene.createObject("Object");
    STAR_CHECK(object && scene.findComponent<star::Transform>(*object) != nullptr);
    const auto removed = scene.queueRemoveComponent(*object, star::componentTypeId<star::Transform>());
    STAR_CHECK(!removed && removed.error() == star::SceneError::RequiredComponent);
    STAR_CHECK(scene.queueAddComponent(*object, star::componentTypeId<star::Camera>()));
    STAR_CHECK(scene.queueAddComponent(*object, star::componentTypeId<star::Light>()));
    STAR_CHECK(scene.queueAddComponent(*object, star::componentTypeId<star::MeshRenderer>()));
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.findComponent<star::Camera>(*object)->near_plane > 0.0F);
    STAR_CHECK(scene.findComponent<star::Light>(*object)->intensity == 1.0F);
    STAR_CHECK(scene.findComponent<star::MeshRenderer>(*object)->cast_shadows);
}

} // namespace

namespace {

enum class LifecycleEvent { Created, Enabled, Begun, Stopped, DestroyedPlay, UnloadedPlay, Disabled, Destroyed };

struct LifecycleLog {
    std::array<LifecycleEvent, 24> events{};
    std::size_t count = 0;
    bool had_transform = false;
    bool queued_only = false;
    star::SpawnToken nested;

    void record(LifecycleEvent event) noexcept { events[count++] = event; }

    bool matches(std::initializer_list<LifecycleEvent> expected) const noexcept {
        return std::ranges::equal(std::span(events.data(), count), expected);
    }
};

class LifecycleObject final : public star::GameObject {
  public:
    explicit LifecycleObject(LifecycleLog &log, bool spawn_on_create = false) noexcept
        : m_log(log), m_spawn_on_create(spawn_on_create) {}

  private:
    void onCreate() noexcept override {
        m_log.record(LifecycleEvent::Created);
        m_log.had_transform = tryGetComponent<const star::Transform>() != nullptr;
        if (m_spawn_on_create) {
            const auto nested = getScene().queueSpawn("Nested");
            if (nested) {
                m_log.nested = *nested;
                const auto commit = getScene().commitCommands();
                m_log.queued_only = !commit && commit.error() == star::SceneError::Busy;
            }
        }
    }

    void onEnable() noexcept override { m_log.record(LifecycleEvent::Enabled); }
    void onBeginPlay() noexcept override { m_log.record(LifecycleEvent::Begun); }

    void onEndPlay(star::EndPlayReason reason) noexcept override {
        using enum star::EndPlayReason;
        switch (reason) {
        case WorldStopped:
            m_log.record(LifecycleEvent::Stopped);
            break;
        case Destroyed:
            m_log.record(LifecycleEvent::DestroyedPlay);
            break;
        case SceneUnloaded:
            m_log.record(LifecycleEvent::UnloadedPlay);
            break;
        }
    }

    void onDisable() noexcept override { m_log.record(LifecycleEvent::Disabled); }
    void onDestroy() noexcept override { m_log.record(LifecycleEvent::Destroyed); }

    LifecycleLog &m_log;
    bool m_spawn_on_create;
};

STAR_TEST(sceneSpawnsDerivedObjectsAndDefersLifecycleCommandsToNextCommit) {
    using enum LifecycleEvent;
    LifecycleLog log;
    star::World world;
    const auto created = world.createScene();
    STAR_CHECK(created && world.beginPlay());
    auto &scene = **created;
    const auto token = scene.queueSpawn("Derived", std::make_unique<LifecycleObject>(log, true));
    STAR_CHECK(token && scene.resolve(*token) == star::ObjectHandle{});
    STAR_CHECK(scene.commitCommands());
    const auto handle = scene.resolve(*token);
    STAR_CHECK(scene.findObject(handle) != nullptr);
    STAR_CHECK(log.matches({Created, Enabled, Begun}));
    STAR_CHECK(log.had_transform && log.queued_only);
    STAR_CHECK(scene.resolve(log.nested) == star::ObjectHandle{});
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(scene.resolve(log.nested) != star::ObjectHandle{});

    const auto guid = scene.findObject(handle)->getGuid();
    STAR_CHECK(scene.findObject(handle)->destroy());
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(log.matches({Created, Enabled, Begun, DestroyedPlay, Disabled, Destroyed}));
    STAR_CHECK(scene.resolve(*token) == star::ObjectHandle{});
    STAR_CHECK(scene.createObject("Restored", guid));
    STAR_CHECK(scene.resolve(*token) == star::ObjectHandle{});
}

STAR_TEST(worldTransitionsPlayAndPropagatesHierarchyActivity) {
    using enum LifecycleEvent;
    LifecycleLog log;
    star::World world;
    const auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    const auto parent = scene.createObject("Parent");
    const auto token = scene.queueSpawn("Child", std::make_unique<LifecycleObject>(log));
    STAR_CHECK(parent && token && scene.commitCommands());
    const auto child = scene.resolve(*token);
    STAR_CHECK(scene.queueSetParent(child, *parent));
    scene.findObject(*parent)->setActive(false);
    STAR_CHECK(world.beginPlay());
    STAR_CHECK(!scene.findObject(child)->isActiveInHierarchy());
    STAR_CHECK(log.matches({Created, Enabled, Disabled}));
    scene.findObject(*parent)->setActive(true);
    STAR_CHECK(scene.commitCommands());
    STAR_CHECK(log.matches({Created, Enabled, Disabled, Enabled, Begun}));
    STAR_CHECK(world.endPlay() && world.endPlay());
    STAR_CHECK(world.beginPlay());
    STAR_CHECK(world.unloadScene(scene));
    STAR_CHECK(
        log.matches({Created, Enabled, Disabled, Enabled, Begun, Stopped, Begun, UnloadedPlay, Disabled, Destroyed}));
}

} // namespace

namespace {

class TestSystem final : public star::System {
  public:
    explicit TestSystem(std::move_only_function<void(star::SystemContext &) noexcept> run) : m_run(std::move(run)) {}

    void execute(star::SystemContext &context) noexcept override { m_run(context); }

  private:
    std::move_only_function<void(star::SystemContext &) noexcept> m_run;
};

STAR_TEST(worldSchedulesSystemsAndCommitsBetweenPhases) {
    using namespace star;
    auto position = componentInfo<scene_test::Position>(1);
    World world;
    const auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    STAR_CHECK(scene.registerComponent(position));
    const auto handle = scene.createObject("Object");
    STAR_CHECK(handle);
    const auto transform = componentTypeId<Transform>();
    constexpr reflect::TypeId writer_id = 1;
    constexpr reflect::TypeId reader_id = 2;
    constexpr reflect::TypeId physics_id = 3;
    bool writer_ok = false;
    bool reader_ok = false;
    int steps = 0;
    SystemDescriptor reader;
    reader.id = reader_id;
    reader.phase = TickPhase::PrePhysics;
    reader.reads = {transform, position.id};
    reader.after = {writer_id};
    reader.parallel = true;
    STAR_CHECK(world.registerSystem(
        std::move(reader), std::make_unique<TestSystem>([&](SystemContext &ctx) noexcept {
            float written = 0;
            int components = 0;
            const auto queried = ctx.each<const Transform>(
                [&](ObjectHandle, const Transform &value) noexcept { written = value.position.x; });
            const auto deferred =
                ctx.each<const scene_test::Position>([&](ObjectHandle, const auto &) noexcept { ++components; });
            const auto denied = ctx.each<Transform>([](ObjectHandle, Transform &) noexcept {});
            reader_ok = queried && deferred && !denied && denied.error() == SceneError::UndeclaredAccess &&
                        written == 3.0F && components == 0;
        })));
    SystemDescriptor writer;
    writer.id = writer_id;
    writer.phase = TickPhase::PrePhysics;
    writer.writes = {transform};
    writer.parallel = true;
    STAR_CHECK(world.registerSystem(std::move(writer), std::make_unique<TestSystem>([&](SystemContext &ctx) noexcept {
                                        const auto queried =
                                            ctx.each<Transform>([&](ObjectHandle object, Transform &value) noexcept {
                                                value.position.x = 3.0F;
                                                writer_ok =
                                                    ctx.queueAddComponent(object, scene_test::Position{9}).has_value();
                                            });
                                        writer_ok = writer_ok && queried.has_value();
                                    })));
    SystemDescriptor physics;
    physics.id = physics_id;
    physics.phase = TickPhase::Physics;
    physics.reads = {position.id};
    STAR_CHECK(world.registerSystem(std::move(physics), std::make_unique<TestSystem>([&](SystemContext &ctx) noexcept {
                                        const auto visible = ctx.each<const scene_test::Position>(
                                            [&](ObjectHandle, const auto &value) noexcept {
                                                if (value.value == 9 && ctx.getDeltaTime() == 0.25F) {
                                                    ++steps;
                                                }
                                            });
                                        if (!visible) {
                                            steps = -100;
                                        }
                                    })));

    STAR_CHECK(world.configureFixedStep(0.25F, 2));
    STAR_CHECK(world.beginPlay());
    STAR_CHECK(world.tick(1.0F));
    STAR_CHECK(writer_ok && reader_ok);
    STAR_CHECK(steps == 2);
    STAR_CHECK(scene.findWorldTransform(*handle)->matrix.transformPoint({}).x == 3.0F);
}

class TickingObject final : public star::GameObject {
  public:
    TickingObject(int id, std::array<int, 8> &log, std::size_t &count) noexcept : m_id(id), m_log(log), m_count(count) {
        setTickEnabled(true);
    }

  private:
    void onTick(float) noexcept override { m_log[m_count++] = m_id; }
    int m_id;
    std::array<int, 8> &m_log;
    std::size_t &m_count;
};

STAR_TEST(worldRunsObjectPrerequisitesAndRejectsCycles) {
    using namespace star;
    std::array<int, 8> log{};
    std::size_t count = 0;
    World world;
    const auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    const auto second = scene.queueSpawn("Second", std::make_unique<TickingObject>(2, log, count));
    const auto first = scene.queueSpawn("First", std::make_unique<TickingObject>(1, log, count));
    STAR_CHECK(first && second && world.beginPlay());
    auto *first_object = scene.findObject(scene.resolve(*first));
    auto *second_object = scene.findObject(scene.resolve(*second));
    STAR_CHECK(second_object->addTickPrerequisite(*first_object));
    STAR_CHECK(world.tick(0));
    STAR_CHECK(count == 2 && log[0] == 1 && log[1] == 2);
    STAR_CHECK(first_object->addTickPrerequisite(*second_object));
    const auto failed = world.tick(0);
    STAR_CHECK(!failed && failed.error() == SceneError::ScheduleCycle && count == 2);
}

} // namespace

namespace {

constexpr auto persisted_scene = R"yaml(
StarFile: Scene
Version: 1
Scene: {Name: Forest}
GameObjects:
  - Guid: "00000000000000000000000000000003"
    Order: 2
    Name: Second
    Components:
      - {Type: "star::Hierarchy", parent: "00000000000000000000000000000001", sibling_index: 1}
  - Guid: "00000000000000000000000000000001"
    Order: 0
    Name: Root
    Components:
      - {Type: "star::Transform", position: [2.5, 0, 0], FutureField: 10}
      - {Type: "game::FutureComponent", Version: 3, future: "kept"}
  - Guid: "00000000000000000000000000000002"
    Order: 1
    Name: First
    Type: "game::FutureObject"
    Properties: {modified: true}
    Components:
      - {Type: "star::Hierarchy", parent: "00000000000000000000000000000001", sibling_index: 0}
)yaml";

STAR_TEST(sceneYamlRoundTripsHierarchyAndUnknownArchiveData) {
    using namespace star;
    using namespace star::serialization;
    World world;
    const auto root_guid = Guid::parse("00000000000000000000000000000001");
    const auto first_guid = Guid::parse("00000000000000000000000000000002");
    STAR_CHECK(root_guid && first_guid);
    const auto strict = loadSceneYaml(world, persisted_scene, LoadPolicy::Strict);
    STAR_CHECK(!strict && strict.error().code == ArchiveErrorCode::UnknownField);
    const auto loaded = loadSceneYaml(world, persisted_scene);
    STAR_CHECK(loaded && loaded->diagnostics.size() >= 3);
    const auto *root = loaded->scene->findObject(*root_guid);
    const auto *child = loaded->scene->findObject(*first_guid);
    STAR_CHECK(root != nullptr && child != nullptr && child->getParent() == root);
    STAR_CHECK(loaded->scene->findHierarchy(root->getHandle())->first_child == child->getHandle());
    STAR_CHECK(child->getWorldTransform().matrix.transformPoint({}).x == 2.5F);

    const auto saved = saveSceneYaml(*loaded->scene);
    STAR_CHECK(saved);
    STAR_CHECK(saved->contains("FutureField") && saved->contains("game::FutureComponent") &&
               saved->contains("game::FutureObject") && saved->contains("modified") && saved->contains("Forest"));
    const auto reloaded = loadSceneYaml(world, *saved);
    STAR_CHECK(reloaded);
    const auto resaved = saveSceneYaml(*reloaded->scene);
    STAR_CHECK(resaved && *resaved == *saved);
}

STAR_TEST(prefabInstantiatesFreshGuidsAndAppliesOverrides) {
    using namespace star;
    using namespace star::serialization;
    World world;
    const auto created = world.createScene();
    STAR_CHECK(created);
    auto &scene = **created;
    const auto parent = scene.createObject("Parent");
    const auto child = scene.createObject("Child");
    STAR_CHECK(parent && child && scene.queueSetParent(*child, *parent, false) && scene.commitCommands());
    const auto prefab = savePrefabYaml(scene, *parent);
    STAR_CHECK(prefab);
    const auto source_root = scene.findObject(*parent)->getGuid();
    const auto prefab_asset = Guid::generate();
    const std::array overrides{
        PrefabOverride{source_root, "Name", ArchiveValue{std::string("Instance")}},
        PrefabOverride{
            source_root, "Components/star::Transform/position",
            ArchiveValue{ArchiveValue::Sequence{ArchiveValue{PlainScalar{"8"}}, ArchiveValue{PlainScalar{"0"}},
                                                ArchiveValue{PlainScalar{"0"}}}}},
    };
    const auto first = instantiatePrefabYaml(scene, *prefab, prefab_asset, overrides, LoadPolicy::Strict);
    const auto second = instantiatePrefabYaml(scene, *prefab, prefab_asset);
    STAR_CHECK(first && second && first->root != second->root);
    const auto *instance = scene.findObject(first->root);
    STAR_CHECK(instance->getGuid() != source_root && instance->getName() == "Instance");
    const auto instance_child = scene.findHierarchy(first->root)->first_child;
    STAR_CHECK(scene.findObject(instance_child)->getParent() == instance);
    STAR_CHECK(scene.findWorldTransform(instance_child)->matrix.transformPoint({}).x == 8.0F);
    STAR_CHECK(!savePrefabYaml(scene, first->root));
    const auto saved = saveSceneYaml(scene);
    STAR_CHECK(saved && saved->contains("PrefabInstance") && saved->contains("RootGuidInPrefab"));
}

STAR_TEST(sceneYamlRejectsMalformedReferencesWithoutChangingExistingScenes) {
    using namespace star;
    World world;
    const auto scene = world.createScene();
    STAR_CHECK(scene && (*scene)->createObject("Existing"));
    const auto failed = loadSceneYaml(world, R"yaml(
StarFile: Scene
Version: 1
GameObjects:
  - Guid: "00000000000000000000000000000001"
    Name: Cycle
    Components:
      - {Type: "star::Hierarchy", parent: "00000000000000000000000000000001"}
)yaml");
    STAR_CHECK(!failed && (*scene)->getObjectCount() == 1);
}

} // namespace

STAR_TEST(staticRenderSnapshotResolvesAssetsAndBakesWorldTransforms) {
    using namespace star;
    struct TemporaryModel {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("star-scene-" + Guid::generate().toString() + ".smdl");
        ~TemporaryModel() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } file;

    StaticModelAsset model;
    model.textures.push_back({TextureColorSpace::Color, {{1, 1, {255, 255, 255, 255}}}});
    auto &part = model.parts.emplace_back();
    part.vertices = {{{0, 0, 0}, {0, 0, 1}, {}}, {{1, 0, 0}, {0, 0, 1}, {}}, {{0, 1, 0}, {0, 0, 1}, {}}};
    part.indices = {0, 1, 2};
    part.material.emplace().base_color_texture = 0;
    const auto bytes = writeModelAsset(model).value();
    {
        std::ofstream output(file.path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        STAR_CHECK(output.good());
    }

    World world;
    auto &scene = *world.createScene().value();
    const auto root = scene.createObject("Root").value();
    const auto mirrored = scene.createObject("Mirrored").value();
    const auto filtered = scene.createObject("Filtered").value();
    scene.findComponent<Transform>(root)->position.x = 3;
    scene.findComponent<Transform>(mirrored)->scale.x = -2;
    STAR_CHECK(scene.queueSetParent(mirrored, root, false));
    const auto mesh_id = Guid::generate();
    const MeshRenderer original{mesh_id, {}, true, 1};
    const MeshRenderer reflection{mesh_id, {}, false, 1};
    const MeshRenderer hidden{Guid::generate(), {}, true, 2};
    STAR_CHECK(scene.queueAddComponent(root, componentTypeId<MeshRenderer>(), &original));
    STAR_CHECK(scene.queueAddComponent(mirrored, componentTypeId<MeshRenderer>(), &reflection));
    STAR_CHECK(scene.queueAddComponent(filtered, componentTypeId<MeshRenderer>(), &hidden));
    STAR_CHECK(scene.commitCommands());

    AssetManager assets;
    const auto missing = extractStaticRenderSnapshot(scene, assets, 1);
    STAR_CHECK(!missing && missing.error().code == StaticRenderErrorCode::AssetLoadFailed);
    STAR_CHECK(assets.registerAsset<StaticModelAsset>(mesh_id, file.path));
    const auto snapshot = extractStaticRenderSnapshot(scene, assets, 1).value();
    STAR_CHECK(snapshot.model.parts.size() == 2);
    STAR_CHECK(snapshot.model.textures.size() == 1);
    const auto mirrored_part = snapshot.shadow_casters.front() ? 1u : 0u;
    const auto &geometry = snapshot.model.parts[mirrored_part];
    STAR_CHECK(geometry.vertices.at(1).position.x == 1.0F);
    STAR_CHECK(geometry.vertices.at(1).normal.z == 1.0F);
    STAR_CHECK(geometry.indices == std::vector<std::uint32_t>({0, 2, 1}));
    STAR_CHECK(geometry.material->base_color_texture == 0);

    STAR_CHECK(scene.queueDestroy(mirrored));
    STAR_CHECK(extractStaticRenderSnapshot(scene, assets, 1).value().model.parts.size() == 1);
}

namespace {

struct SnapshotFixture {
    star::StaticModelAsset model;
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("star-scene-" + star::Guid::generate().toString() + ".smdl");
    ~SnapshotFixture() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    star::Guid write() {
        auto &part = model.parts.emplace_back();
        part.vertices = {{{0, 0, 0}, {0, 0, 1}, {}}, {{1, 0, 0}, {0, 0, 1}, {}}, {{0, 1, 0}, {0, 0, 1}, {}}};
        part.indices = {0, 1, 2};
        const auto bytes = star::writeModelAsset(model).value();
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        return star::Guid::generate();
    }
};

bool closeTo(float left, float right) { return std::abs(left - right) < 1e-5F; }

bool closeTo(star::math::Vec3 left, star::math::Vec3 right) {
    return closeTo(left.x, right.x) && closeTo(left.y, right.y) && closeTo(left.z, right.z);
}

} // namespace

STAR_TEST(staticRenderSnapshotCarriesCamerasLightsAndBounds) {
    using namespace star;
    SnapshotFixture fixture;
    const auto mesh_id = fixture.write();

    World world;
    auto &scene = *world.createScene().value();
    const auto mesh = scene.createObject("Mesh").value();
    const MeshRenderer renderer{mesh_id, {}, true, 1};
    STAR_CHECK(scene.queueAddComponent(mesh, componentTypeId<MeshRenderer>(), &renderer));

    const auto camera_object = scene.createObject("Camera").value();
    const Camera camera{0.9F, 0.2F, 90.0F};
    STAR_CHECK(scene.queueAddComponent(camera_object, componentTypeId<Camera>(), &camera));
    auto *camera_transform = scene.findComponent<Transform>(camera_object);
    camera_transform->position = {4.0F, 3.0F, 2.0F};

    const auto sun_object = scene.createObject("Sun").value();
    const Light sun{true, {1.0F, 0.95F, 0.9F}, 2.5F, 1.0F};
    STAR_CHECK(scene.queueAddComponent(sun_object, componentTypeId<Light>(), &sun));
    const auto sun_rotation = math::Quaternion::fromAxisAngle({1.0F, 0.0F, 0.0F}, math::pi * 0.5F);
    scene.findComponent<Transform>(sun_object)->rotation = sun_rotation;

    const auto lamp = scene.createObject("Lamp").value();
    const Light point{{}, {0.2F, 0.4F, 1.0F}, 12.0F, 6.0F};
    STAR_CHECK(scene.queueAddComponent(lamp, componentTypeId<Light>(), &point));
    scene.findComponent<Transform>(lamp)->position = {1.0F, 2.0F, 3.0F};

    STAR_CHECK(scene.commitCommands());
    AssetManager assets;
    STAR_CHECK(assets.registerAsset<StaticModelAsset>(mesh_id, fixture.path));

    const auto snapshot = extractStaticRenderSnapshot(scene, assets).value();
    STAR_CHECK(snapshot.cameras.size() == 1);
    const auto &entry = snapshot.cameras.front();
    STAR_CHECK(closeTo(entry.vertical_fov, 0.9F));
    STAR_CHECK(closeTo(entry.near_plane, 0.2F));
    STAR_CHECK(closeTo(entry.far_plane, 90.0F));
    STAR_CHECK(closeTo(entry.world.columns[3].x, 4.0F));
    STAR_CHECK(closeTo(entry.world.columns[3].y, 3.0F));
    STAR_CHECK(closeTo(entry.world.columns[3].z, 2.0F));

    STAR_CHECK(snapshot.directional_light.has_value());
    STAR_CHECK(
        closeTo(snapshot.directional_light->direction, math::normalize(sun_rotation.rotate({0.0F, 0.0F, 1.0F}))));
    STAR_CHECK(closeTo(snapshot.directional_light->intensity, 2.5F));

    STAR_CHECK(snapshot.point_lights.size() == 1);
    STAR_CHECK(closeTo(snapshot.point_lights.front().position, {1.0F, 2.0F, 3.0F}));
    STAR_CHECK(closeTo(snapshot.point_lights.front().range, 6.0F));

    STAR_CHECK(closeTo(snapshot.bounds.center, {0.5F, 0.5F, 0.0F}));
    STAR_CHECK(closeTo(snapshot.bounds.radius, std::sqrt(2.0F) * 0.5F));
}

STAR_TEST(staticRenderSnapshotCapsPointLights) {
    using namespace star;
    SnapshotFixture fixture;
    const auto mesh_id = fixture.write();
    World world;
    auto &scene = *world.createScene().value();
    const auto mesh = scene.createObject("Mesh").value();
    const MeshRenderer renderer{mesh_id, {}, true, 1};
    STAR_CHECK(scene.queueAddComponent(mesh, componentTypeId<MeshRenderer>(), &renderer));
    const Light point{};
    for (std::size_t index = 0; index < renderer::kMaxRenderPointLights + 3; ++index) {
        const auto lamp = scene.createObject("Lamp").value();
        STAR_CHECK(scene.queueAddComponent(lamp, componentTypeId<Light>(), &point));
    }
    STAR_CHECK(scene.commitCommands());
    AssetManager assets;
    STAR_CHECK(assets.registerAsset<StaticModelAsset>(mesh_id, fixture.path));
    const auto snapshot = extractStaticRenderSnapshot(scene, assets).value();
    STAR_CHECK(snapshot.point_lights.size() == renderer::kMaxRenderPointLights);
}

STAR_TEST(staticRenderSnapshotRejectsInvalidCameraAndLight) {
    using namespace star;
    SnapshotFixture fixture;
    const auto mesh_id = fixture.write();
    World world;
    auto &scene = *world.createScene().value();
    const auto mesh = scene.createObject("Mesh").value();
    const MeshRenderer renderer{mesh_id, {}, true, 1};
    STAR_CHECK(scene.queueAddComponent(mesh, componentTypeId<MeshRenderer>(), &renderer));
    const auto camera_object = scene.createObject("Camera").value();
    const Camera camera{0.0F, 0.1F, 10.0F};
    STAR_CHECK(scene.queueAddComponent(camera_object, componentTypeId<Camera>(), &camera));
    STAR_CHECK(scene.commitCommands());
    AssetManager assets;
    STAR_CHECK(assets.registerAsset<StaticModelAsset>(mesh_id, fixture.path));
    const auto invalid_camera = extractStaticRenderSnapshot(scene, assets);
    STAR_CHECK(!invalid_camera && invalid_camera.error().code == StaticRenderErrorCode::InvalidCamera);

    STAR_CHECK(scene.queueRemoveComponent(camera_object, componentTypeId<Camera>()));
    const auto lamp = scene.createObject("Lamp").value();
    const Light invalid_light{{}, {1.0F, 1.0F, 1.0F}, 1.0F, 0.0F};
    STAR_CHECK(scene.queueAddComponent(lamp, componentTypeId<Light>(), &invalid_light));
    STAR_CHECK(scene.commitCommands());
    const auto invalid_lighting = extractStaticRenderSnapshot(scene, assets);
    STAR_CHECK(!invalid_lighting && invalid_lighting.error().code == StaticRenderErrorCode::InvalidLight);
}

STAR_TEST(staticRenderSnapshotAppliesInstanceMaterials) {
    using namespace star;
    SnapshotFixture fixture;
    const auto mesh_id = fixture.write();
    const auto material_id = Guid::generate();
    const auto material_path =
        std::filesystem::temp_directory_path() / ("star-scene-" + Guid::generate().toString() + ".mat");
    {
        std::ofstream material(material_path, std::ios::binary);
        material << "Type: star::Material\n"
                    "Version: 1\n"
                    "Properties:\n"
                    "  Shader: "
                 << Guid::generate().toString()
                 << "\n"
                    "  Parameters:\n"
                    "    BaseColor: [0.2, 0.4, 0.6, 0.5]\n"
                    "    Metallic: 0.75\n"
                    "    Roughness: 0.15\n"
                    "  RenderState:\n"
                    "    Blend: Transparent\n"
                    "    Shading: Lit\n"
                    "    DoubleSided: false\n"
                    "    DepthTest: true\n";
        STAR_CHECK(material.good());
    }

    World world;
    auto &scene = *world.createScene().value();
    const auto mesh = scene.createObject("Mesh").value();
    const MeshRenderer renderer{mesh_id, {material_id}, true, 1};
    STAR_CHECK(scene.queueAddComponent(mesh, componentTypeId<MeshRenderer>(), &renderer));
    STAR_CHECK(scene.commitCommands());

    AssetManager assets;
    STAR_CHECK(assets.registerAsset<StaticModelAsset>(mesh_id, fixture.path));
    STAR_CHECK(assets.registerAsset<MaterialAsset>(material_id, material_path));

    const auto snapshot = extractStaticRenderSnapshot(scene, assets).value();
    STAR_CHECK(snapshot.model.parts.size() == 1);
    const auto &material = snapshot.model.parts.front().material;
    STAR_CHECK(material.has_value());
    STAR_CHECK((material->base_color == std::array<float, 4>{0.2F, 0.4F, 0.6F, 0.5F}));
    STAR_CHECK(material->metallic == 0.75F);
    STAR_CHECK(material->roughness == 0.15F);
    STAR_CHECK(material->blend == MaterialBlendMode::Transparent);

    const auto mismatch = scene.findComponent<MeshRenderer>(mesh);
    STAR_CHECK(mismatch != nullptr);
    star::World second_world;
    auto &second_scene = *second_world.createScene().value();
    const auto second_mesh = second_scene.createObject("Mesh").value();
    const MeshRenderer conflicting{mesh_id, {material_id, material_id}, true, 1};
    STAR_CHECK(second_scene.queueAddComponent(second_mesh, componentTypeId<MeshRenderer>(), &conflicting));
    STAR_CHECK(second_scene.commitCommands());
    const auto invalid = extractStaticRenderSnapshot(second_scene, assets);
    STAR_CHECK(!invalid && invalid.error().code == StaticRenderErrorCode::InvalidMaterial);

    std::error_code error;
    std::filesystem::remove(material_path, error);
}
