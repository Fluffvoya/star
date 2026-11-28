#include "editor-ui/editor_shell.hpp"

#include "core/log.hpp"
#include "editor/command.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace star::editor {
namespace {

const LogCategory k_log_category("EditorShell", LogLevel::Info);

struct ConsoleEntry {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

[[nodiscard]] ImVec4 level_color(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return ImVec4(0.55F, 0.55F, 0.55F, 1.0F);
    case LogLevel::Debug:
        return ImVec4(0.70F, 0.70F, 0.70F, 1.0F);
    case LogLevel::Info:
        return ImVec4(0.85F, 0.85F, 0.85F, 1.0F);
    case LogLevel::Warning:
        return ImVec4(0.95F, 0.75F, 0.25F, 1.0F);
    case LogLevel::Error:
        return ImVec4(0.95F, 0.35F, 0.35F, 1.0F);
    case LogLevel::Fatal:
        return ImVec4(1.00F, 0.20F, 0.20F, 1.0F);
    }
    return ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
}

[[nodiscard]] bool is_read_only_string(const reflect::TypeInfo *type) {
    if (type == nullptr) {
        return false;
    }
    return type->name == "star::Guid" || type->name == "star::Name" || type->name == "std::filesystem::path";
}

} // namespace

struct EditorShell::Impl {
    bool quit = false;
    bool layout_initialized = false;
    bool show_console = true;
    bool show_stats = true;
    bool show_hierarchy = true;
    bool show_inspector = true;
    bool show_content = true;
    bool show_demo = false;
    bool auto_scroll = true;
    LogLevel minimum_level = LogLevel::Info;
    log::LogSinkId sink = 0;
    std::mutex mutex;
    std::deque<ConsoleEntry> entries;

    std::shared_ptr<EditorDocument> document;
    std::filesystem::path project_root;
    std::vector<std::filesystem::path> scenes;
    double next_scan = 0.0;
    char rename_buffer[128] = {};
    Guid rename_target;

    static void on_log(const log::LogRecord &record, void *user_data) {
        auto *self = static_cast<Impl *>(user_data);
        if (self == nullptr) {
            return;
        }
        std::scoped_lock lock(self->mutex);
        try {
            self->entries.push_back(
                ConsoleEntry{record.level, std::string(record.category), std::string(record.message)});
        } catch (...) {
            return;
        }
        constexpr std::size_t maximum_entries = 4096;
        while (self->entries.size() > maximum_entries) {
            self->entries.pop_front();
        }
    }

    void save_document() {
        if (!document) {
            return;
        }
        std::filesystem::path path = document->getPath();
        if (path.empty()) {
            const std::filesystem::path content = project_root.empty() ? std::filesystem::path(".") : project_root;
            path = content / "Content" / "Scenes" / (document->getName() + ".scene");
        }
        if (const auto saved = document->save(path); !saved) {
            log::error(k_log_category, "Save failed: {}", saved.error().message);
            return;
        }
        log::info(k_log_category, "Scene saved to {}", path.string());
        next_scan = 0.0;
    }

    void rescan_content() {
        scenes.clear();
        const std::filesystem::path root =
            (project_root.empty() ? std::filesystem::path(".") : project_root) / "Content";
        std::error_code error;
        if (!std::filesystem::is_directory(root, error)) {
            return;
        }
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        if (error) {
            return;
        }
        try {
            for (const auto &entry : iterator) {
                if (scenes.size() >= 512) {
                    break;
                }
                if (!entry.is_regular_file(error)) {
                    continue;
                }
                if (entry.path().extension() == ".scene") {
                    scenes.push_back(entry.path());
                }
            }
        } catch (...) {
            // A partially listed project is still usable.
        }
        std::sort(scenes.begin(), scenes.end());
    }

    void sync_name_buffer() {
        const GameObject *selected = document ? document->findSelected() : nullptr;
        if (selected == nullptr) {
            rename_target = {};
            rename_buffer[0] = '\0';
            return;
        }
        if (rename_target != selected->getGuid()) {
            rename_target = selected->getGuid();
            const std::string &name = selected->getName();
            const std::size_t length = std::min(name.size(), sizeof(rename_buffer) - 1);
            std::memcpy(rename_buffer, name.data(), length);
            rename_buffer[length] = '\0';
        }
    }

    void draw_menu_bar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                if (auto created = EditorDocument::createEmpty("Untitled"); created) {
                    document = *created;
                    rename_target = {};
                } else {
                    log::error(k_log_category, "New scene failed: {}", created.error().message);
                }
            }
            if (ImGui::MenuItem("Save", "Ctrl+S", false, static_cast<bool>(document))) {
                save_document();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                quit = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            const std::string undo_label =
                document && document->canUndo() ? std::format("Undo {}", document->getUndoName()) : "Undo";
            const std::string redo_label =
                document && document->canRedo() ? std::format("Redo {}", document->getRedoName()) : "Redo";
            if (ImGui::MenuItem(undo_label.c_str(), "Ctrl+Z", false, document && document->canUndo())) {
                document->undo();
                rename_target = {};
            }
            if (ImGui::MenuItem(redo_label.c_str(), "Ctrl+Y", false, document && document->canRedo())) {
                document->redo();
                rename_target = {};
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy);
            ImGui::MenuItem("Inspector", nullptr, &show_inspector);
            ImGui::MenuItem("Content", nullptr, &show_content);
            ImGui::MenuItem("Console", nullptr, &show_console);
            ImGui::MenuItem("Statistics", nullptr, &show_stats);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &show_demo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("About", nullptr, false, false);
            ImGui::EndMenu();
        }
        if (document) {
            const char *dirty = document->isDirty() ? " *" : "";
            ImGui::Separator();
            ImGui::TextDisabled("%s%s", document->getName().c_str(), dirty);
        }
        ImGui::EndMainMenuBar();
    }

    void build_default_layout(ImGuiID dockspace_id) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID center = dockspace_id;
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30F, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30F, nullptr, &center);
        ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45F, nullptr, &right);
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22F, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Content", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Statistics", right_bottom);
        ImGui::DockBuilderFinish(dockspace_id);
        layout_initialized = true;
    }

    void draw_hierarchy() {
        if (!show_hierarchy) {
            return;
        }
        if (!ImGui::Begin("Hierarchy", &show_hierarchy)) {
            ImGui::End();
            return;
        }
        if (document) {
            if (ImGui::Button("Create Object")) {
                document->execute(std::make_unique<CreateObjectCommand>("Object"));
            }
            ImGui::SameLine();
            const bool has_selection = document->findSelected() != nullptr;
            if (ImGui::Button("Delete") && has_selection) {
                const Guid target = document->getSelection().front();
                document->execute(std::make_unique<DeleteObjectCommand>(target));
                rename_target = {};
            }
            ImGui::Separator();
            Scene &scene = document->getScene();
            int row = 0;
            for (const ObjectHandle handle : scene.getObjects()) {
                const GameObject *object = scene.findObject(handle);
                if (object == nullptr) {
                    continue;
                }
                const Guid guid = object->getGuid();
                const bool selected = std::find(document->getSelection().begin(), document->getSelection().end(),
                                                guid) != document->getSelection().end();
                ImGui::PushID(row++);
                if (ImGui::Selectable(object->getName().c_str(), selected)) {
                    document->select(guid, ImGui::GetIO().KeyCtrl);
                    rename_target = {};
                }
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("No scene open");
        }
        ImGui::End();
    }

    void push_property_command(EditorDocument &target, const Guid &object, reflect::TypeId component,
                               const std::vector<std::string> &path, std::string text) {
        target.execute(std::make_unique<SetPropertyCommand>(object, component, path, std::move(text)));
    }

    void draw_properties(EditorDocument &target, const Guid &object, reflect::TypeId component,
                         const reflect::TypeInfo &type, void *value, std::vector<std::string> &path) {
        for (const reflect::PropertyInfo &property : type.properties) {
            const std::string name(property.name);
            path.push_back(name);
            void *member = property.get_mutable(value);
            if (member == nullptr) {
                path.pop_back();
                continue;
            }
            const reflect::TypeInfo *nested = property.nestedType();
            if (property.container != nullptr) {
                ImGui::LabelText(name.c_str(), "%zu item(s)", property.container->size(member));
                path.pop_back();
                continue;
            }
            switch (property.kind) {
            case reflect::PropertyKind::Boolean: {
                bool current = *static_cast<bool *>(member);
                if (ImGui::Checkbox(name.c_str(), &current)) {
                    push_property_command(target, object, component, path, current ? "true" : "false");
                }
                break;
            }
            case reflect::PropertyKind::FloatingPoint: {
                if (nested != nullptr && nested->size == sizeof(double)) {
                    double current = *static_cast<double *>(member);
                    ImGui::InputScalar(name.c_str(), ImGuiDataType_Double, &current);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        push_property_command(target, object, component, path, std::format("{}", current));
                    }
                } else {
                    float current = *static_cast<float *>(member);
                    ImGui::DragFloat(name.c_str(), &current, 0.02F);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        push_property_command(target, object, component, path, std::format("{}", current));
                    }
                }
                break;
            }
            case reflect::PropertyKind::SignedInteger: {
                const std::size_t size = nested != nullptr ? nested->size : sizeof(std::int32_t);
                std::int64_t current = 0;
                std::memcpy(&current, member, std::min(size, sizeof(current)));
                ImGui::InputScalar(name.c_str(), size > sizeof(std::int32_t) ? ImGuiDataType_S64 : ImGuiDataType_S32,
                                   &current);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    push_property_command(target, object, component, path, std::format("{}", current));
                }
                break;
            }
            case reflect::PropertyKind::UnsignedInteger: {
                const std::size_t size = nested != nullptr ? nested->size : sizeof(std::uint32_t);
                std::uint64_t current = 0;
                std::memcpy(&current, member, std::min(size, sizeof(current)));
                ImGui::InputScalar(name.c_str(), size > sizeof(std::uint32_t) ? ImGuiDataType_U64 : ImGuiDataType_U32,
                                   &current);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    push_property_command(target, object, component, path, std::format("{}", current));
                }
                break;
            }
            case reflect::PropertyKind::String: {
                const std::string current = *static_cast<const std::string *>(member);
                ImGui::LabelText(name.c_str(), "%s", current.c_str());
                break;
            }
            case reflect::PropertyKind::Reflected: {
                if (nested == nullptr) {
                    ImGui::LabelText(name.c_str(), "(unknown type)");
                    break;
                }
                if (nested->kind == reflect::TypeKind::Enum) {
                    std::uint64_t enum_value = 0;
                    if (nested->enum_ops.get != nullptr) {
                        enum_value = nested->enum_ops.get(member);
                    }
                    std::string label = std::format("{}", enum_value);
                    for (const reflect::EnumEntry &entry : nested->enum_entries) {
                        if (entry.value == enum_value) {
                            label = std::string(entry.name);
                            break;
                        }
                    }
                    ImGui::LabelText(name.c_str(), "%s", label.c_str());
                } else if (nested->kind == reflect::TypeKind::Struct) {
                    if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        draw_properties(target, object, component, *nested, member, path);
                        ImGui::TreePop();
                    }
                } else {
                    ImGui::LabelText(name.c_str(), "%s", std::string(nested->name).c_str());
                }
                break;
            }
            }
            path.pop_back();
        }
    }

    void draw_inspector() {
        if (!show_inspector) {
            return;
        }
        if (!ImGui::Begin("Inspector", &show_inspector)) {
            ImGui::End();
            return;
        }
        GameObject *selected = document ? document->findSelected() : nullptr;
        if (document == nullptr || selected == nullptr) {
            ImGui::TextDisabled("Select an object in the hierarchy");
            ImGui::End();
            return;
        }
        sync_name_buffer();
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::InputText("##name", rename_buffer, sizeof(rename_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            document->execute(std::make_unique<RenameObjectCommand>(selected->getGuid(), rename_buffer));
        }
        ImGui::TextDisabled("Guid %s", selected->getGuid().toString().c_str());

        Scene &scene = document->getScene();
        const ObjectHandle handle = selected->getHandle();
        for (const reflect::TypeId component : scene.getComponentTypes(handle)) {
            const reflect::TypeInfo *type = scene.findComponentType(component);
            void *value = scene.findComponent(handle, component);
            if (type == nullptr || value == nullptr) {
                continue;
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader(std::string(type->name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                std::vector<std::string> path;
                draw_properties(*document, selected->getGuid(), component, *type, value, path);
            }
        }
        ImGui::End();
    }

    void draw_content() {
        if (!show_content) {
            return;
        }
        if (!ImGui::Begin("Content", &show_content)) {
            ImGui::End();
            return;
        }
        if (ImGui::Button("Refresh")) {
            next_scan = 0.0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", project_root.empty() ? "." : project_root.string().c_str());
        ImGui::Separator();
        if (scenes.empty()) {
            ImGui::TextDisabled("No .scene files under Content/");
        }
        std::shared_ptr<EditorDocument> pending;
        for (const std::filesystem::path &scene : scenes) {
            const std::string label = scene.filename().string();
            ImGui::PushID(scene.string().c_str());
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (auto opened = EditorDocument::open(scene); opened) {
                        pending = *opened;
                    } else {
                        log::error(k_log_category, "Open failed: {}", opened.error().message);
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", scene.string().c_str());
            }
            ImGui::PopID();
        }
        if (pending) {
            document = std::move(pending);
            rename_target = {};
        }
        ImGui::End();
    }

    void draw_console() {
        if (!show_console) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(720.0F, 240.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Console", &show_console)) {
            ImGui::End();
            return;
        }
        if (ImGui::Button("Clear")) {
            std::scoped_lock lock(mutex);
            entries.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &auto_scroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0F);
        int level = static_cast<int>(minimum_level);
        const char *levels[] = {"Trace", "Debug", "Info", "Warning", "Error", "Fatal"};
        if (ImGui::Combo("Level", &level, levels, IM_ARRAYSIZE(levels))) {
            minimum_level = static_cast<LogLevel>(level);
        }
        ImGui::Separator();

        std::vector<ConsoleEntry> snapshot;
        {
            std::scoped_lock lock(mutex);
            snapshot.assign(entries.begin(), entries.end());
        }
        ImGui::BeginChild("ConsoleScroll", ImVec2(0.0F, 0.0F), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const ConsoleEntry &entry : snapshot) {
            if (entry.level < minimum_level) {
                continue;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, level_color(entry.level));
            ImGui::TextUnformatted(entry.category.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.message.c_str());
            ImGui::PopStyleColor();
        }
        if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0F) {
            ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void draw_statistics(const EditorShellStatus &status) {
        if (!show_stats) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(320.0F, 240.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Statistics", &show_stats)) {
            ImGui::End();
            return;
        }
        ImGui::Text("Frame %.2f ms", static_cast<double>(status.frame_time_ms));
        ImGui::Text("FPS %.1f", static_cast<double>(status.fps));
        ImGui::Separator();
        ImGui::Text("Viewport %u x %u", status.width, status.height);
        ImGui::Text("ImGui draw calls %u", status.draw_calls);
        if (document) {
            ImGui::Text("Objects %zu", document->getScene().getObjectCount());
        }
        if (!status.device_name.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("GPU %.*s", static_cast<int>(status.device_name.size()), status.device_name.data());
        }
        ImGui::Separator();
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::Text("Renderer: star-rhi");
        ImGui::End();
    }

    void handle_shortcuts() const {
        if (!ImGui::GetIO().KeyCtrl) {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && document) {
            const_cast<Impl *>(this)->save_document();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && document && document->canUndo()) {
            document->undo();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && document && document->canRedo()) {
            document->redo();
        }
    }

    void draw_frame(const EditorShellStatus &status, double now) {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
        if (!layout_initialized) {
            build_default_layout(dockspace_id);
        }
        if (now >= next_scan) {
            rescan_content();
            next_scan = now + 2.0;
        }
        handle_shortcuts();
        draw_menu_bar();
        draw_hierarchy();
        draw_inspector();
        draw_content();
        draw_console();
        draw_statistics(status);
        if (show_demo) {
            ImGui::ShowDemoWindow(&show_demo);
        }
    }
};

EditorShell::EditorShell() : m_impl(std::make_unique<Impl>()) {
    m_impl->sink = log::addSink(&Impl::on_log, m_impl.get());
    log::info(k_log_category, "Editor shell ready");
}

EditorShell::~EditorShell() {
    if (m_impl && m_impl->sink != 0) {
        log::removeSink(m_impl->sink);
    }
}

void EditorShell::setDocument(std::shared_ptr<EditorDocument> document) {
    m_impl->document = std::move(document);
    m_impl->rename_target = {};
}

std::shared_ptr<EditorDocument> EditorShell::getDocument() const { return m_impl ? m_impl->document : nullptr; }

void EditorShell::setProjectRoot(std::filesystem::path root) {
    m_impl->project_root = std::move(root);
    m_impl->next_scan = 0.0;
}

const std::filesystem::path &EditorShell::getProjectRoot() const noexcept {
    static const std::filesystem::path empty;
    return m_impl ? m_impl->project_root : empty;
}

void EditorShell::draw(const EditorShellStatus &status) { m_impl->draw_frame(status, ImGui::GetTime()); }

bool EditorShell::shouldQuit() const noexcept { return !m_impl || m_impl->quit; }

void EditorShell::requestQuit() noexcept {
    if (m_impl) {
        m_impl->quit = true;
    }
}

} // namespace star::editor
