#include "editor/document.hpp"

#include "core/log.hpp"
#include "editor/command.hpp"
#include "scene/scene_archive.hpp"
#include "scene/world.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace star::editor {
namespace {

const LogCategory k_log_category("EditorDocument", LogLevel::Info);

constexpr std::size_t k_max_undo = 256;

[[nodiscard]] std::string read_text_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

EditorDocument::EditorDocument() = default;

EditorDocument::~EditorDocument() {
    if (m_world && m_world->isPlaying()) {
        static_cast<void>(m_world->endPlay());
    }
    m_undo.clear();
    m_selection.clear();
    m_scene = nullptr;
    m_world.reset();
}

std::expected<void, EditorError> EditorDocument::loadGameModule(const std::filesystem::path &path) {
    if (!path.empty()) {
        const auto loaded = m_world->loadGameModule(path);
        if (!loaded) {
            return std::unexpected(
                EditorError{EditorErrorCode::GameModuleFailed,
                            std::format("Cannot load {} ({})", path.string(), static_cast<int>(loaded.error()))});
        }
        m_game_module = path;
    }
    return {};
}

std::expected<std::shared_ptr<EditorDocument>, EditorError>
EditorDocument::createEmpty(std::string name, const std::filesystem::path &game_module) {
    auto document = std::shared_ptr<EditorDocument>(new EditorDocument());
    document->m_world = std::make_unique<World>();
    if (auto loaded = document->loadGameModule(game_module); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    auto scene = document->m_world->createScene();
    if (!scene) {
        return std::unexpected(EditorError{EditorErrorCode::SceneCreationFailed, "Failed to create a scene"});
    }
    document->m_scene = *scene;
    document->m_name = std::move(name);
    return document;
}

std::expected<std::shared_ptr<EditorDocument>, EditorError>
EditorDocument::open(const std::filesystem::path &scene_path, const std::filesystem::path &game_module) {
    const std::string source = read_text_file(scene_path);
    if (source.empty()) {
        return std::unexpected(EditorError{EditorErrorCode::FileReadFailed, "Cannot read " + scene_path.string()});
    }
    auto document = std::shared_ptr<EditorDocument>(new EditorDocument());
    document->m_world = std::make_unique<World>();
    if (auto loaded = document->loadGameModule(game_module); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    auto loaded = loadSceneYaml(*document->m_world, source);
    if (!loaded) {
        return std::unexpected(
            EditorError{EditorErrorCode::SceneLoadFailed, "Scene parse failed in " + scene_path.string()});
    }
    document->m_scene = loaded->scene;
    document->m_path = scene_path;
    document->m_name = scene_path.stem().string();
    log::info(k_log_category, "Opened {} with {} objects and {} diagnostics", scene_path.string(),
              document->m_scene->getObjectCount(), loaded->diagnostics.size());
    return document;
}

std::expected<std::shared_ptr<EditorDocument>, EditorError> EditorDocument::createPlayCopy() {
    if (m_world->isPlaying() || !m_scene->commitCommands()) {
        return std::unexpected(EditorError{EditorErrorCode::PlayFailed, "The editor scene is not at a safe point"});
    }
    const auto snapshot = saveSceneBinary(*m_scene);
    if (!snapshot) {
        return std::unexpected(
            EditorError{EditorErrorCode::PlayFailed, "Cannot capture scene at " + snapshot.error().path});
    }
    auto play = std::shared_ptr<EditorDocument>(new EditorDocument());
    play->m_world = std::make_unique<World>();
    if (auto loaded = play->loadGameModule(m_game_module); !loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    auto restored = loadSceneBinary(*play->m_world, *snapshot);
    if (!restored) {
        return std::unexpected(
            EditorError{EditorErrorCode::PlayFailed, "Cannot restore scene at " + restored.error().path});
    }
    play->m_scene = restored->scene;
    play->m_name = m_name;
    for (const auto &guid : m_selection) {
        if (play->m_scene->findObject(guid)) {
            play->m_selection.push_back(guid);
        }
    }
    if (!play->m_world->beginPlay()) {
        return std::unexpected(EditorError{EditorErrorCode::PlayFailed, "Cannot begin play"});
    }
    return play;
}

std::expected<void, EditorError> EditorDocument::save(const std::filesystem::path &scene_path) {
    auto yaml = saveSceneYaml(*m_scene);
    if (!yaml) {
        return std::unexpected(EditorError{EditorErrorCode::SceneSaveFailed, "Scene serialization failed"});
    }
    std::error_code error;
    if (const auto parent = scene_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, error);
    }
    const auto staging = scene_path.parent_path() / (".star-save-" + Guid::generate().toString());
    if (error || !std::filesystem::create_directory(staging, error)) {
        return std::unexpected(
            EditorError{EditorErrorCode::FileWriteFailed, "Cannot stage save for " + scene_path.string()});
    }
    const auto temporary = staging / "scene";
    std::ofstream file(temporary, std::ios::binary);
    file.write(yaml->data(), static_cast<std::streamsize>(yaml->size()));
    file.close();
    const bool written = file.good();
    if (written) {
        std::filesystem::rename(temporary, scene_path, error);
    }
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    std::filesystem::remove(staging, cleanup_error);
    if (!written || error) {
        return std::unexpected(
            EditorError{EditorErrorCode::FileWriteFailed, "Write failed for " + scene_path.string()});
    }
    m_path = scene_path;
    m_name = scene_path.stem().string();
    m_dirty = false;
    return {};
}

std::expected<void, EditorError> EditorDocument::save() {
    if (m_path.empty()) {
        return std::unexpected(EditorError{EditorErrorCode::FileWriteFailed, "The scene has no path yet"});
    }
    return save(m_path);
}

void EditorDocument::execute(std::unique_ptr<EditorCommand> command) {
    if (!command) {
        return;
    }
    if (!command->execute(*this)) {
        log::warning(k_log_category, "Command {} failed", command->getName());
        return;
    }
    if (m_undo_index < m_undo.size()) {
        m_undo.erase(m_undo.begin() + static_cast<std::ptrdiff_t>(m_undo_index), m_undo.end());
    }
    m_undo.push_back(std::move(command));
    ++m_undo_index;
    if (m_undo.size() > k_max_undo) {
        m_undo.erase(m_undo.begin());
        --m_undo_index;
    }
}

bool EditorDocument::undo() {
    if (m_undo_index == 0) {
        return false;
    }
    --m_undo_index;
    m_undo[m_undo_index]->undo(*this);
    return true;
}

bool EditorDocument::redo() {
    if (m_undo_index >= m_undo.size()) {
        return false;
    }
    if (!m_undo[m_undo_index]->execute(*this)) {
        return false;
    }
    ++m_undo_index;
    return true;
}

std::string_view EditorDocument::getUndoName() const noexcept {
    return m_undo_index == 0 ? std::string_view{} : m_undo[m_undo_index - 1]->getName();
}

std::string_view EditorDocument::getRedoName() const noexcept {
    return m_undo_index >= m_undo.size() ? std::string_view{} : m_undo[m_undo_index]->getName();
}

bool EditorDocument::hasSelection(const Guid &guid) const noexcept {
    return std::find(m_selection.begin(), m_selection.end(), guid) != m_selection.end();
}

void EditorDocument::select(const Guid &guid, bool additive) {
    if (!additive) {
        m_selection.clear();
        m_selection.push_back(guid);
        return;
    }
    const auto found = std::find(m_selection.begin(), m_selection.end(), guid);
    if (found != m_selection.end()) {
        m_selection.erase(found);
    } else {
        m_selection.push_back(guid);
    }
}

GameObject *EditorDocument::findSelected() const noexcept {
    if (m_selection.empty() || m_scene == nullptr) {
        return nullptr;
    }
    return m_scene->findObject(m_selection.front());
}

} // namespace star::editor
