#include "test_support.hpp"

#include "scene/world.hpp"

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

    scene.commitCommands();
    scene.commitCommands();
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
    scene.commitCommands();
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
            scene.commitCommands();
            was_deferred = removed.has_value() && scene.findComponent<Velocity>(handle) == &speed;
            const auto blocked = scene.createObject("Invalid");
            was_protected = !blocked && blocked.error() == star::SceneError::Busy && !world.unloadScene(scene);
        });
    STAR_CHECK(queried && visits == 1);
    STAR_CHECK(was_deferred && was_protected);
    STAR_CHECK(scene.findComponent<Position>(*first)->value == 9);
    scene.commitCommands();
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
    scene.commitCommands();
    STAR_CHECK(scene.queueAddComponent(*object, component.id));
    STAR_CHECK(Counted::count == 2);
    STAR_CHECK(scene.unregisterComponents(component.owner));
    STAR_CHECK(Counted::count == 0);
    STAR_CHECK(scene.findComponent<Counted>(*object) == nullptr);
    scene.commitCommands();
    STAR_CHECK(scene.registerComponent(component));
    STAR_CHECK(scene.queueAddComponent(*object, component.id));
    STAR_CHECK(scene.queueDestroy(*object));
    scene.commitCommands();
    STAR_CHECK(Counted::count == 0);
}

} // namespace
