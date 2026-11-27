#include "fixtures/game-module/game_module_fixture.hpp"
#include "scene/scene_archive.hpp"
#include "scene/world.hpp"
#include "test_support.hpp"

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace {

STAR_TEST(worldLoadsIndependentGameModuleAndReleasesItsCodeBeforeReload) {
    using namespace star;
    using namespace game_fixture;
    World world;
    const auto before = world.createScene();
    STAR_CHECK(before);
    STAR_CHECK(!world.loadGameModule("invalid-module-path"));
    const auto module = world.loadGameModule(STAR_GAME_MODULE_PATH);
    STAR_CHECK(module && *module == module_id);
    const auto duplicate = world.loadGameModule(STAR_GAME_MODULE_PATH);
    STAR_CHECK(!duplicate && duplicate.error() == GameModuleError::AlreadyLoaded);

    const auto score_id = componentTypeId<Score>();
    const auto retained = (*before)->createObject("Engine-owned");
    STAR_CHECK(retained && (*before)->queueAddComponent(*retained, score_id));

    const auto loaded = loadSceneYaml(world, R"yaml(
StarFile: Scene
Version: 1
GameObjects:
  - Guid: "00000000000000000000000000000001"
    Name: Actor
    Type: "game_fixture::Actor"
    Components:
      - {Type: "game_fixture::Score", value: 5}
)yaml",
                                      serialization::LoadPolicy::Strict);
    STAR_CHECK(loaded);
    const auto guid = Guid::parse("00000000000000000000000000000001");
    STAR_CHECK(guid);
    auto &scene = *loaded->scene;
    auto *actor = scene.findObject(*guid);
    STAR_CHECK(actor != nullptr && actor->tryGetComponent<Score>()->value == 6);
    const auto handle = actor->getHandle();
    const auto kept_child = scene.createObject("Engine-owned child");
    STAR_CHECK(kept_child && scene.queueSetParent(*kept_child, handle, false));
    STAR_CHECK(world.beginPlay());
    actor->tryGetComponent<Score>()->value = 20;
    STAR_CHECK(world.tick(0));
    STAR_CHECK(actor->tryGetComponent<Score>()->value == 130);
    STAR_CHECK((*before)->findComponent<Score>(*retained)->value == 100);
    const auto yaml = saveSceneYaml(scene);
    STAR_CHECK(yaml);
    const auto round_trip = loadSceneYaml(world, *yaml, serialization::LoadPolicy::Strict);
    STAR_CHECK(round_trip);
    STAR_CHECK(round_trip->scene->findObject(*guid)->tryGetComponent<Score>()->value == 131);
    STAR_CHECK(world.unloadScene(*round_trip->scene));

    const auto actor_type = reflect::makeTypeId(object_type);
    const auto pending = scene.queueSpawn("Pending Actor", actor_type);
    const auto pending_component = (*before)->createObject("Pending Component");
    STAR_CHECK(pending && pending_component && (*before)->queueAddComponent(*pending_component, score_id));
    STAR_CHECK(world.unloadGameModule(module_id));
    STAR_CHECK(scene.findObject(*guid) == nullptr && scene.findObject(*kept_child) != nullptr);
    STAR_CHECK(scene.findHierarchy(*kept_child)->parent == ObjectHandle{});
    STAR_CHECK(scene.resolve(*pending) == ObjectHandle{});
    STAR_CHECK((*before)->findComponent<Score>(*retained) == nullptr);
    STAR_CHECK(scene.commitCommands() && (*before)->commitCommands() && world.tick(0));
    STAR_CHECK(!scene.queueSpawn("Unregistered", actor_type));

#if defined(__linux__)
    auto *still_loaded = dlopen(STAR_GAME_MODULE_PATH, RTLD_LAZY | RTLD_NOLOAD);
    if (still_loaded != nullptr) {
        dlclose(still_loaded);
    }
    STAR_CHECK(still_loaded == nullptr);
#endif

    STAR_CHECK(world.loadGameModule(STAR_GAME_MODULE_PATH));
    const auto fresh = scene.queueSpawn("Reloaded Actor", actor_type);
    STAR_CHECK(fresh && scene.commitCommands() && scene.findObject(scene.resolve(*fresh)) != nullptr);
    STAR_CHECK(world.unloadGameModule(module_id));
}

class NoOpSystem final : public star::System {
  public:
    void execute(star::SystemContext &) noexcept override {}
};

STAR_TEST(worldRollsBackRegistrationWhenGameStartupFails) {
    using namespace star;
    using namespace game_fixture;
    World world;
    const auto scene = world.createScene();
    STAR_CHECK(scene);
    const auto object = (*scene)->createObject("Object");
    STAR_CHECK(object);
    SystemDescriptor conflict;
    conflict.id = reflect::makeTypeId("game_fixture::ScoreSystem");
    STAR_CHECK(world.registerSystem(std::move(conflict), std::make_unique<NoOpSystem>(), 10));
    const auto failed = world.loadGameModule(STAR_GAME_MODULE_PATH);
    STAR_CHECK(!failed && failed.error() == GameModuleError::StartupFailed);
    const auto missing = (*scene)->queueAddComponent(*object, componentTypeId<Score>());
    STAR_CHECK(!missing && missing.error() == SceneError::UnknownComponent);
    STAR_CHECK(world.unregisterSystems(10));
    STAR_CHECK(world.loadGameModule(STAR_GAME_MODULE_PATH));
    STAR_CHECK(world.unloadGameModule(module_id));
}

} // namespace
