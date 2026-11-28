#include "test_support.hpp"

#include "editor/command.hpp"
#include "editor/document.hpp"
#include "editor/project.hpp"
#include "scene/components.hpp"
#include "scene/scene_archive.hpp"
#include "scene/world.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#ifdef STAR_GAME_MODULE_PATH
#include "fixtures/game-module/game_module_fixture.hpp"
#if defined(__linux__)
#include <dlfcn.h>
#endif
#endif

namespace {

[[nodiscard]] std::filesystem::path temp_scene_path(std::string_view name) {
    const auto directory = std::filesystem::temp_directory_path() / "star-editor-tests";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory / (std::string(name) + ".scene");
}

} // namespace

STAR_TEST(documentCreatesObjectsWithUndoAndRedo) {
    auto created = star::editor::EditorDocument::createEmpty("Test");
    STAR_CHECK(created);
    std::shared_ptr<star::editor::EditorDocument> document = *created;
    STAR_CHECK(document->getScene().getObjectCount() == 0);

    document->execute(std::make_unique<star::editor::CreateObjectCommand>("Object"));
    STAR_CHECK(document->getScene().getObjectCount() == 1);
    STAR_CHECK(document->canUndo());

    STAR_CHECK(document->undo());
    STAR_CHECK(document->getScene().getObjectCount() == 0);
    STAR_CHECK(document->canRedo());

    STAR_CHECK(document->redo());
    STAR_CHECK(document->getScene().getObjectCount() == 1);
}

STAR_TEST(documentEditsTransformPropertiesThroughCommands) {
    auto created = star::editor::EditorDocument::createEmpty("Test");
    STAR_CHECK(created);
    std::shared_ptr<star::editor::EditorDocument> document = *created;
    document->execute(std::make_unique<star::editor::CreateObjectCommand>("Object"));

    const star::GameObject *object = document->findSelected();
    STAR_CHECK(object != nullptr);
    const star::Guid guid = object->getGuid();
    const auto component = star::componentTypeId<star::Transform>();

    document->execute(std::make_unique<star::editor::SetPropertyCommand>(
        guid, component, std::vector<std::string>{"position", "x"}, "2.5"));
    const auto *transform = document->getScene().findComponent<star::Transform>(object->getHandle());
    STAR_CHECK(transform != nullptr);
    STAR_CHECK(std::abs(transform->position.x - 2.5F) < 0.001F);

    STAR_CHECK(document->undo());
    const auto *restored = document->getScene().findComponent<star::Transform>(object->getHandle());
    STAR_CHECK(restored != nullptr && std::abs(restored->position.x) < 0.001F);

    STAR_CHECK(document->redo());
    const auto *again = document->getScene().findComponent<star::Transform>(object->getHandle());
    STAR_CHECK(again != nullptr && std::abs(again->position.x - 2.5F) < 0.001F);
}

STAR_TEST(documentRenamesAndDeletesWithRestore) {
    auto created = star::editor::EditorDocument::createEmpty("Test");
    STAR_CHECK(created);
    std::shared_ptr<star::editor::EditorDocument> document = *created;
    document->execute(std::make_unique<star::editor::CreateObjectCommand>("Original"));
    const star::GameObject *object = document->findSelected();
    STAR_CHECK(object != nullptr);
    const star::Guid guid = object->getGuid();

    document->execute(std::make_unique<star::editor::RenameObjectCommand>(guid, "Renamed"));
    const star::GameObject *renamed = document->getScene().findObject(guid);
    STAR_CHECK(renamed != nullptr && renamed->getName() == "Renamed");
    STAR_CHECK(document->undo());
    const star::GameObject *restored_name = document->getScene().findObject(guid);
    STAR_CHECK(restored_name != nullptr && restored_name->getName() == "Original");

    document->execute(std::make_unique<star::editor::DeleteObjectCommand>(guid));
    STAR_CHECK(document->getScene().getObjectCount() == 0);
    STAR_CHECK(document->undo());
    STAR_CHECK(document->getScene().getObjectCount() == 1);
    STAR_CHECK(document->getScene().findObject(guid) != nullptr);
}

STAR_TEST(documentSavesAndReopensSceneFiles) {
    auto created = star::editor::EditorDocument::createEmpty("RoundTrip");
    STAR_CHECK(created);
    std::shared_ptr<star::editor::EditorDocument> document = *created;
    document->execute(std::make_unique<star::editor::CreateObjectCommand>("First"));
    const star::GameObject *object = document->findSelected();
    STAR_CHECK(object != nullptr);
    const star::Guid guid = object->getGuid();
    document->execute(std::make_unique<star::editor::SetPropertyCommand>(
        guid, star::componentTypeId<star::Transform>(), std::vector<std::string>{"position", "y"}, "3.0"));

    const std::filesystem::path path = temp_scene_path("round-trip");
    STAR_CHECK(document->save(path));
    STAR_CHECK(!document->isDirty());
    STAR_CHECK(std::filesystem::exists(path));

    auto reopened = star::editor::EditorDocument::open(path);
    STAR_CHECK(reopened);
    const star::Scene &scene = (*reopened)->getScene();
    STAR_CHECK(scene.getObjectCount() == 1);
    const star::GameObject *loaded = scene.findObject(guid);
    STAR_CHECK(loaded != nullptr && loaded->getName() == "First");
    const auto *transform = scene.findComponent<star::Transform>(loaded->getHandle());
    STAR_CHECK(transform != nullptr && std::abs(transform->position.y - 3.0F) < 0.001F);

    document->execute(std::make_unique<star::editor::RenameObjectCommand>(guid, "Second"));
    const auto blocked_path = temp_scene_path(star::Guid::generate().toString());
    STAR_CHECK(std::filesystem::create_directory(blocked_path));
    STAR_CHECK(!document->save(blocked_path));
    STAR_CHECK(document->isDirty());
    STAR_CHECK(document->getPath() == path);
    STAR_CHECK(std::filesystem::is_directory(blocked_path));
    auto unchanged = star::editor::EditorDocument::open(path);
    STAR_CHECK(unchanged && (*unchanged)->getScene().findObject(guid)->getName() == "First");
    STAR_CHECK(document->save());
    auto replaced = star::editor::EditorDocument::open(path);
    STAR_CHECK(replaced && (*replaced)->getScene().findObject(guid)->getName() == "Second");
    std::filesystem::remove(blocked_path);
    std::filesystem::remove(path);
}

STAR_TEST(documentAppliesWorldTransformToChildAsOneUndoableCommand) {
    using namespace star;
    using namespace star::editor;
    auto document = EditorDocument::createEmpty("Transform").value();
    auto &scene = document->getScene();
    auto *parent = scene.findObject(scene.createObject("Parent").value());
    auto *child = scene.findObject(scene.createObject("Child").value());
    STAR_CHECK(parent && child);
    parent->setTransform({.position = {2, 0, 0}, .rotation = {}, .scale = {2, 3, 4}});
    STAR_CHECK(scene.queueSetParent(child->getHandle(), parent->getHandle(), false) && scene.commitCommands());
    auto next_world = child->getWorldTransform().matrix;
    next_world.columns[3].x += 2.0F;
    const auto local = scene.localTransformForWorld(child->getHandle(), next_world);
    STAR_CHECK(local && local->position.x == 1.0F);
    document->execute(std::make_unique<SetTransformCommand>(child->getGuid(), *local));
    STAR_CHECK(scene.commitCommands() && child->getWorldTransform().matrix.columns[3].x == 4.0F);
    STAR_CHECK(document->undo() && scene.commitCommands() && child->getWorldTransform().matrix.columns[3].x == 2.0F);
    STAR_CHECK(document->redo() && scene.commitCommands() && child->getWorldTransform().matrix.columns[3].x == 4.0F);
    const auto child_guid = child->getGuid();
    document->execute(std::make_unique<DeleteObjectCommand>(child_guid));
    STAR_CHECK(scene.findObject(child_guid) == nullptr);
    STAR_CHECK(document->undo());
    child = scene.findObject(child_guid);
    STAR_CHECK(child && child->getParent() == parent && child->getWorldTransform().matrix.columns[3].x == 4.0F);
    parent->setTransform({.position = {}, .rotation = {}, .scale = {0, 1, 1}});
    const auto singular = scene.localTransformForWorld(child->getHandle(), next_world);
    STAR_CHECK(!singular && singular.error() == SceneError::IncompatibleTransform);
}

STAR_TEST(projectSettingsRoundTripWithoutDroppingUnknownData) {
    using namespace star::editor;
    const auto path = temp_scene_path(star::Guid::generate().toString()).replace_extension(".starproject");
    {
        std::ofstream file(path);
        file << R"yaml(StarFile: Project
Version: 1
Name: Example
EngineVersion: "0.1.0"
Modules: ["Build/Game.so"]
StartupScene: "Content/Start.scene"
FutureSetting: {Mode: retained}
)yaml";
        file.close();
        STAR_CHECK(file.good());
    }
    auto project = EditorProject::open(path);
    STAR_CHECK(project);
    STAR_CHECK((*project)->getSettings().name == "Example");
    STAR_CHECK((*project)->getSettings().modules.size() == 1);
    STAR_CHECK((*project)->resolve((*project)->getSettings().modules.front()) == path.parent_path() / "Build/Game.so");
    auto edited = (*project)->getSettings();
    edited.startup_scene = "invalid.txt";
    STAR_CHECK(!(*project)->save(edited));
    STAR_CHECK((*project)->getSettings().startup_scene == "Content/Start.scene");
    edited.startup_scene = "Content/Next.scene";
    edited.name = "Renamed";
    STAR_CHECK((*project)->save(edited));
    auto reopened = EditorProject::open(path);
    STAR_CHECK(reopened && (*reopened)->getSettings().name == "Renamed");
    STAR_CHECK((*reopened)->resolve((*reopened)->getSettings().startup_scene) ==
               path.parent_path() / "Content/Next.scene");
    std::ifstream saved(path);
    const std::string text(std::istreambuf_iterator<char>{saved}, {});
    STAR_CHECK(text.find("FutureSetting") != std::string::npos);
    saved.close();
    std::filesystem::remove(path);
}

#ifdef STAR_GAME_MODULE_PATH
namespace {

void writeGameScene(const std::filesystem::path &path) {
    {
        std::ofstream file(path);
        file << R"yaml(StarFile: Scene
Version: 1
GameObjects:
  - Guid: "00000000000000000000000000000001"
    Name: Actor
    Type: "game_fixture::Actor"
    Components:
      - {Type: "game_fixture::Score", value: 5, future_field: retained}
      - {Type: "future::Unknown", future_value: 99}
)yaml";
        file.close();
        STAR_CHECK(file.good());
    }
}

} // namespace

STAR_TEST(playCopyRunsIndependentGameObjectsAndKeepsEditorState) {
    using namespace star;
    using namespace star::editor;
    const auto path = temp_scene_path(Guid::generate().toString());
    writeGameScene(path);
    auto opened = EditorDocument::open(path, STAR_GAME_MODULE_PATH);
    STAR_CHECK(opened);
    auto &document = **opened;
    const auto guid = *Guid::parse("00000000000000000000000000000001");
    document.select(guid, false);
    document.execute(std::make_unique<RenameObjectCommand>(guid, "Unsaved"));
    STAR_CHECK(document.findSelected()->tryGetComponent<game_fixture::Score>()->value == 6);
    auto created = document.createPlayCopy();
    STAR_CHECK(created);
    auto &play = **created;
    World &world = play.getScene().getWorld();
    STAR_CHECK(world.isPlaying() && !document.getScene().getWorld().isPlaying());
    STAR_CHECK(play.findSelected() != document.findSelected());
    STAR_CHECK(play.findSelected()->getGuid() == guid && play.findSelected()->getName() == "Unsaved");
    const int starting_score = play.findSelected()->tryGetComponent<game_fixture::Score>()->value;
    STAR_CHECK(world.tick(1.0F / 60.0F));
    STAR_CHECK(play.findSelected()->tryGetComponent<game_fixture::Score>()->value == starting_score + 110);
    STAR_CHECK(document.findSelected()->tryGetComponent<game_fixture::Score>()->value == 6);
    const auto yaml = saveSceneYaml(play.getScene());
    STAR_CHECK(yaml && yaml->find("future_field") != std::string::npos &&
               yaml->find("future::Unknown") != std::string::npos);
    play.execute(std::make_unique<RenameObjectCommand>(guid, "Runtime"));
    created->reset();
    STAR_CHECK(document.isDirty() && document.findSelected()->getName() == "Unsaved");
    STAR_CHECK(document.undo() && document.findSelected()->getName() == "Actor");
    STAR_CHECK(document.redo() && document.findSelected()->getName() == "Unsaved");
    std::filesystem::remove(path);
}

STAR_TEST(reloadsGameModuleAndPreservesDocumentAcrossFailure) {
    using namespace star;
    using namespace star::editor;
    const auto path = temp_scene_path(Guid::generate().toString());
    writeGameScene(path);
    const auto recovery = path.parent_path() / "Recovery";
    auto opened = EditorDocument::open(path, STAR_GAME_MODULE_PATH);
    STAR_CHECK(opened);
    auto &document = **opened;
    const auto guid = *Guid::parse("00000000000000000000000000000001");
    const auto score = componentTypeId<game_fixture::Score>();
    document.select(guid, false);
    document.execute(std::make_unique<RenameObjectCommand>(guid, "Unsaved"));
    document.execute(std::make_unique<SetPropertyCommand>(guid, score, std::vector<std::string>{"value"}, "42"));
    document.execute(std::make_unique<RenameObjectCommand>(guid, "Pending Redo"));
    STAR_CHECK(document.undo() && document.canRedo());
    auto play = document.createPlayCopy().value();
    bool stopped = false;
    const auto stop = [&] {
        play.reset();
        stopped = true;
    };
    STAR_CHECK(!document.reloadGameModule(path, recovery, stop));
    STAR_CHECK(!stopped && play->getScene().getWorld().isPlaying());

    const auto failed = document.reloadGameModule(STAR_FAILING_MODULE_PATH, recovery, stop);
    STAR_CHECK(!failed && failed.error().code == EditorErrorCode::ReloadFailed);
    STAR_CHECK(stopped && !play && !document.isRecovering());
    STAR_CHECK(document.findSelected()->tryGetComponent<game_fixture::Score>()->value == 42);
    STAR_CHECK(document.findSelected()->getName() == "Unsaved" && document.isDirty());
    STAR_CHECK(document.undo() && document.findSelected()->tryGetComponent<game_fixture::Score>()->value == 6);
    STAR_CHECK(document.redo() && document.findSelected()->tryGetComponent<game_fixture::Score>()->value == 42);
    STAR_CHECK(document.canRedo());

    STAR_CHECK(document.reloadGameModule(STAR_RELOAD_MODULE_PATH, recovery));
    STAR_CHECK(document.getGameModulePath() == STAR_RELOAD_MODULE_PATH);
    const auto *migrated = document.findSelected()->tryGetComponent<game_fixture::Score>();
    STAR_CHECK(migrated && migrated->value == 42 && migrated->bonus == 7);
    const auto yaml = saveSceneYaml(document.getScene());
    STAR_CHECK(yaml && yaml->find("future_field") != std::string::npos &&
               yaml->find("future::Unknown") != std::string::npos);
    STAR_CHECK(document.isDirty() && document.findSelected()->getGuid() == guid);
    STAR_CHECK(!document.undo() && document.canUndo() && document.canRedo());
    STAR_CHECK(document.redo() && document.findSelected()->getName() == "Pending Redo");
    auto replay = document.createPlayCopy();
    STAR_CHECK(replay && (*replay)->getScene().getWorld().tick(1.0F / 60.0F));
    STAR_CHECK(document.findSelected()->tryGetComponent<game_fixture::Score>()->value == 42);
    std::ifstream source(path);
    const std::string text(std::istreambuf_iterator<char>{source}, {});
    STAR_CHECK(text.find("Name: Actor") != std::string::npos);
    source.close();
    std::filesystem::remove(path);
}

#if defined(__linux__)
STAR_TEST(reloadRetainsSnapshotWhenBothModuleVersionsFail) {
    using namespace star;
    using namespace star::editor;
    const auto dir = temp_scene_path(Guid::generate().toString()).replace_extension();
    STAR_CHECK(std::filesystem::create_directory(dir));
    const auto module_path = dir / "game.so";
    STAR_CHECK(std::filesystem::copy_file(STAR_GAME_MODULE_PATH, module_path));
    const auto path = dir / "game.scene";
    writeGameScene(path);
    auto document = EditorDocument::open(path, module_path).value();
    const auto guid = *Guid::parse("00000000000000000000000000000001");
    document->select(guid, false);
    document->execute(std::make_unique<RenameObjectCommand>(guid, "Recover Me"));
    std::filesystem::path old_image;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().filename().string().starts_with(".star-module-")) {
            old_image = entry.path();
        }
    }
    STAR_CHECK(!old_image.empty() && std::filesystem::remove(old_image));
    const auto recovery = dir / "Recovery";
    const auto failed = document->reloadGameModule(STAR_FAILING_MODULE_PATH, recovery);
    STAR_CHECK(!failed && failed.error().code == EditorErrorCode::RecoveryRequired);
    STAR_CHECK(document->isRecovering() && !document->save() && !document->createPlayCopy());
    STAR_CHECK(std::filesystem::is_directory(recovery));
    bool snapshot_written = false;
    bool diagnostic_written = false;
    for (const auto &entry : std::filesystem::directory_iterator(recovery)) {
        snapshot_written |= entry.path().extension() == ".snapshot" && entry.file_size() > 0;
        diagnostic_written |= entry.path().extension() == ".log" && entry.file_size() > 0;
    }
    STAR_CHECK(snapshot_written && diagnostic_written);
    void *still_loaded = dlopen(old_image.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (still_loaded) {
        dlclose(still_loaded);
    }
    STAR_CHECK(!still_loaded);
    STAR_CHECK(document->reloadGameModule(module_path, recovery));
    STAR_CHECK(!document->isRecovering() && document->isDirty());
    STAR_CHECK(document->findSelected()->getName() == "Recover Me" && document->undo());
    STAR_CHECK(document->findSelected()->getName() == "Actor");
    document.reset();
    std::filesystem::remove_all(dir);
}
#endif
#endif
