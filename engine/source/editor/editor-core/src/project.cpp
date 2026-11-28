#include "editor/project.hpp"

#include "file_io.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace star::editor {
namespace {

using namespace serialization;

constexpr std::string_view k_type_name = "star::editor::ProjectSettings";

EditorError invalidProject(const std::filesystem::path &path, std::string detail) {
    return {EditorErrorCode::InvalidProject, path.string() + ": " + std::move(detail)};
}

std::expected<void, EditorError> validateSettings(const std::filesystem::path &path, const ProjectSettings &settings) {
    if (settings.name.empty()) {
        return std::unexpected(invalidProject(path, "Name is required"));
    }
    if (!settings.startup_scene.empty() && settings.startup_scene.extension() != ".scene") {
        return std::unexpected(invalidProject(path, "StartupScene must name a .scene file"));
    }
    if (std::ranges::any_of(settings.modules, [](const auto &module) { return module.empty(); })) {
        return std::unexpected(invalidProject(path, "Modules cannot contain an empty path"));
    }
    return {};
}

} // namespace

const reflect::TypeInfo &projectSettingsType() {
    using reflect::makeProperty;
    static const std::array properties{
        makeProperty<ProjectSettings, std::string, &ProjectSettings::name>("Name"),
        makeProperty<ProjectSettings, std::string, &ProjectSettings::engine_version>("EngineVersion"),
        makeProperty<ProjectSettings, std::vector<std::filesystem::path>, &ProjectSettings::modules>("Modules"),
        makeProperty<ProjectSettings, std::filesystem::path, &ProjectSettings::startup_scene>("StartupScene"),
    };
    static const reflect::TypeInfo type{reflect::makeTypeId(k_type_name),
                                        k_type_name,
                                        reflect::TypeKind::Struct,
                                        sizeof(ProjectSettings),
                                        alignof(ProjectSettings),
                                        properties,
                                        reflect::makeTypeOps<ProjectSettings>(),
                                        reflect::builtin_module,
                                        1,
                                        nullptr};
    return type;
}

std::expected<std::shared_ptr<EditorProject>, EditorError> EditorProject::open(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(EditorError{EditorErrorCode::FileReadFailed, "Cannot read " + path.string()});
    }
    const std::string bytes(std::istreambuf_iterator<char>{file}, {});
    auto yaml = readYamlValue(bytes);
    auto *mapping = yaml ? std::get_if<ArchiveValue::Mapping>(&yaml->value) : nullptr;
    if (!mapping) {
        return std::unexpected(invalidProject(path, "Expected a project YAML mapping"));
    }
    const auto file_kind = mapping->extract("StarFile");
    const auto version = mapping->extract("Version");
    if (file_kind.empty() || file_kind.mapped() != ArchiveValue{std::string{"Project"}} || version.empty() ||
        version.mapped() != ArchiveValue{PlainScalar{"1"}}) {
        return std::unexpected(invalidProject(path, "Expected StarFile: Project and Version: 1"));
    }
    auto project = std::make_shared<EditorProject>();
    const auto result =
        applyObject({std::string{k_type_name}, 1, std::move(*mapping)}, projectSettingsType(), &project->m_settings);
    if (!result) {
        return std::unexpected(invalidProject(path, "Cannot read setting " + result.error().path));
    }
    for (const auto &diagnostic : result->diagnostics) {
        if (diagnostic.code != ArchiveErrorCode::UnknownField) {
            return std::unexpected(invalidProject(path, "Invalid setting " + diagnostic.path));
        }
    }
    if (const auto valid = validateSettings(path, project->m_settings); !valid) {
        return std::unexpected(valid.error());
    }
    project->m_path = path;
    project->m_extensions = std::move(result->extensions);
    return project;
}

std::filesystem::path EditorProject::resolve(const std::filesystem::path &relative) const {
    return relative.empty() ? std::filesystem::path{} : (getRoot() / relative).lexically_normal();
}

std::expected<void, EditorError> EditorProject::save(ProjectSettings settings) {
    if (auto valid = validateSettings(m_path, settings); !valid) {
        return valid;
    }
    auto captured = captureObject(projectSettingsType(), &settings, m_extensions);
    if (!captured) {
        return std::unexpected(invalidProject(m_path, "Cannot serialize setting " + captured.error().path));
    }
    captured->fields.insert_or_assign("StarFile", ArchiveValue{std::string{"Project"}});
    captured->fields.insert_or_assign("Version", ArchiveValue{PlainScalar{"1"}});
    auto yaml = writeYamlValue(ArchiveValue{std::move(captured->fields)});
    if (!yaml) {
        return std::unexpected(invalidProject(m_path, "Cannot encode project settings"));
    }
    if (auto written = writeEditorFile(m_path, *yaml); !written) {
        return written;
    }
    m_settings = std::move(settings);
    return {};
}

} // namespace star::editor
