#include "test_support.hpp"

#include "editor/command.hpp"
#include "editor/document.hpp"
#include "scene/components.hpp"
#include "scene/scene_archive.hpp"
#include "scene/world.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

#ifdef STAR_GAME_MODULE_PATH
#include "fixtures/game-module/game_module_fixture.hpp"
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

#ifdef STAR_GAME_MODULE_PATH
STAR_TEST(playCopyRunsIndependentGameObjectsAndKeepsEditorState) {
    using namespace star;
    using namespace star::editor;
    const auto path = temp_scene_path(Guid::generate().toString());
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
        STAR_CHECK(file.good());
    }
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
#endif
