#pragma once

#include "editor/export.hpp"
#include "scene/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace star::editor {

enum class EditorErrorCode : std::uint8_t {
    SceneCreationFailed,
    SceneLoadFailed,
    SceneSaveFailed,
    FileReadFailed,
    FileWriteFailed,
};

struct EditorError {
    EditorErrorCode code;
    std::string message;
};

class EditorDocument;
class EditorCommand;

// An editable scene plus selection, dirty state and an undo stack.
// All methods run on the thread that owns the document.
class STAR_EDITORCORE_API EditorDocument final {
  public:
    [[nodiscard]] static std::expected<std::shared_ptr<EditorDocument>, EditorError> createEmpty(std::string name);
    [[nodiscard]] static std::expected<std::shared_ptr<EditorDocument>, EditorError>
    open(const std::filesystem::path &scene_path);

    ~EditorDocument();
    EditorDocument(const EditorDocument &) = delete;
    EditorDocument &operator=(const EditorDocument &) = delete;

    [[nodiscard]] Scene &getScene() const noexcept { return *m_scene; }
    [[nodiscard]] const std::filesystem::path &getPath() const noexcept { return m_path; }
    [[nodiscard]] const std::string &getName() const noexcept { return m_name; }
    [[nodiscard]] bool isDirty() const noexcept { return m_dirty; }
    void markDirty() noexcept { m_dirty = true; }

    [[nodiscard]] std::expected<void, EditorError> save(const std::filesystem::path &scene_path);
    [[nodiscard]] std::expected<void, EditorError> save();

    // Runs the command and pushes it. Clears the redo tail first.
    void execute(std::unique_ptr<EditorCommand> command);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool canUndo() const noexcept { return m_undo_index > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_undo_index < m_undo.size(); }
    [[nodiscard]] std::string_view getUndoName() const noexcept;
    [[nodiscard]] std::string_view getRedoName() const noexcept;

    void select(const Guid &guid, bool additive);
    void clearSelection() noexcept { m_selection.clear(); }
    [[nodiscard]] const std::vector<Guid> &getSelection() const noexcept { return m_selection; }
    [[nodiscard]] GameObject *findSelected() const noexcept;

  private:
    EditorDocument();
    [[nodiscard]] bool hasSelection(const Guid &guid) const noexcept;

    std::unique_ptr<World> m_world;
    Scene *m_scene = nullptr;
    std::filesystem::path m_path;
    std::string m_name;
    bool m_dirty = false;
    std::vector<Guid> m_selection;
    std::vector<std::unique_ptr<EditorCommand>> m_undo;
    std::size_t m_undo_index = 0;
};

} // namespace star::editor
