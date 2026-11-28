#include "test_support.hpp"

#include "editor/command.hpp"
#include "editor/document.hpp"
#include "scene/components.hpp"

#include <cmath>
#include <filesystem>
#include <memory>

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
