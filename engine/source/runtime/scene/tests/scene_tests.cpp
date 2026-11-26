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
