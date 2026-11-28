#pragma once

#include "editor-ui/export.hpp"
#include "editor/document.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace star::editor {

struct EditorShellStatus {
    float frame_time_ms = 0.0F;
    float fps = 0.0F;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t draw_calls = 0;
    std::string_view device_name;
};

// Main frame of the editor: menu bar, docking layout and editor panels.
class STAR_EDITORUI_API EditorShell final {
  public:
    EditorShell();
    ~EditorShell();
    EditorShell(const EditorShell &) = delete;
    EditorShell &operator=(const EditorShell &) = delete;

    void setDocument(std::shared_ptr<EditorDocument> document);
    [[nodiscard]] std::shared_ptr<EditorDocument> getDocument() const;
    void setProjectRoot(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path &getProjectRoot() const noexcept;

    void draw(const EditorShellStatus &status);

    [[nodiscard]] bool shouldQuit() const noexcept;
    void requestQuit();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace star::editor
