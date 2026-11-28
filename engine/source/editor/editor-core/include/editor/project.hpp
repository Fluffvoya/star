#pragma once

#include "editor/document.hpp"
#include "serialization/archive.hpp"

namespace star::editor {

struct ProjectSettings {
    std::string name;
    std::string engine_version;
    std::vector<std::filesystem::path> modules;
    std::filesystem::path startup_scene;
};

[[nodiscard]] STAR_EDITORCORE_API const reflect::TypeInfo &projectSettingsType();

class STAR_EDITORCORE_API EditorProject final {
  public:
    [[nodiscard]] static std::expected<std::shared_ptr<EditorProject>, EditorError>
    open(const std::filesystem::path &path);

    [[nodiscard]] const ProjectSettings &getSettings() const noexcept { return m_settings; }
    [[nodiscard]] const std::filesystem::path &getPath() const noexcept { return m_path; }
    [[nodiscard]] std::filesystem::path getRoot() const { return m_path.parent_path(); }
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path &relative) const;
    [[nodiscard]] std::expected<void, EditorError> save(ProjectSettings settings);

  private:
    ProjectSettings m_settings;
    std::filesystem::path m_path;
    serialization::ArchiveValue::Mapping m_extensions;
};

} // namespace star::editor
