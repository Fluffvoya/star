#include "editor-ui/editor_shell.hpp"

#include "core/log.hpp"
#include "editor/command.hpp"
#include "scene/world.hpp"
#include "scene_viewport.hpp"

#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <map>
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

} // namespace

struct EditorShell::Impl {
    bool quit = false;
    bool quit_requested = false;
    bool confirm_transition = false;
    std::shared_ptr<EditorDocument> next_document;
    std::string save_error;
    bool layout_initialized = false;
    bool show_console = true;
    bool show_stats = true;
    bool show_hierarchy = true;
    bool show_inspector = true;
    bool show_content = true;
    bool show_viewport = true;
    bool show_demo = false;
    bool auto_scroll = true;
    LogLevel minimum_level = LogLevel::Info;
    log::LogSinkId sink = 0;
    std::mutex mutex;
    std::deque<ConsoleEntry> entries;

    std::shared_ptr<EditorDocument> document;
    std::shared_ptr<EditorDocument> play_document;
    bool play_paused = false;
    bool simulate = false;
    bool step_requested = false;
    std::string reload_status;
    std::unique_ptr<SceneViewport> scene_viewport;
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

    bool saveDocument() {
        if (!document) {
            return false;
        }
        save_error.clear();
        auto path = document->getPath();
        if (path.empty()) {
            const auto directory = project_root / "Content" / "Scenes";
            const auto name = document->getName();
            path = directory / (name + ".scene");
            std::error_code error;
            for (std::size_t suffix = 2; std::filesystem::exists(path, error); ++suffix) {
                path = directory / std::format("{} {}.scene", name, suffix);
            }
            if (error) {
                save_error = error.message();
                log::error(k_log_category, "Cannot choose a scene path: {}", save_error);
                return false;
            }
        }
        if (const auto saved = document->save(path); !saved) {
            save_error = saved.error().message;
            log::error(k_log_category, "Save failed: {}", save_error);
            return false;
        }
        log::info(k_log_category, "Scene saved to {}", path.string());
        next_scan = 0.0;
        return true;
    }

    [[nodiscard]] EditorDocument *activeDocument() const {
        return play_document ? play_document.get() : document.get();
    }

    [[nodiscard]] std::filesystem::path gameModulePath() const {
        return document ? document->getGameModulePath() : std::filesystem::path{};
    }

    void stopPlay() {
        play_document.reset();
        play_paused = false;
        step_requested = false;
        rename_target = {};
    }

    void drawPlayControls() {
        if (ImGui::MenuItem("Play", nullptr, false, document && !document->isRecovering() && !play_document)) {
            if (auto play = document->createPlayCopy(); play) {
                play_document = std::move(*play);
                rename_target = {};
            } else {
                log::error(k_log_category, "Play failed: {}", play.error().message);
            }
        }
        if (ImGui::MenuItem(play_paused ? "Resume" : "Pause", nullptr, false, static_cast<bool>(play_document))) {
            play_paused = !play_paused;
        }
        if (ImGui::MenuItem("Step", nullptr, false, play_document && play_paused)) {
            step_requested = true;
        }
        if (ImGui::MenuItem("Stop", nullptr, false, static_cast<bool>(play_document))) {
            stopPlay();
        }
        ImGui::Separator();
        ImGui::MenuItem("Simulation Camera", nullptr, &simulate);
    }

    void tickPlay(float delta) {
        if (!play_document || (play_paused && !step_requested)) {
            return;
        }
        auto &world = play_document->getScene().getWorld();
        const float elapsed = step_requested ? 1.0F / 60.0F : std::clamp(delta, 0.0F, 0.1F);
        step_requested = false;
        if (const auto ticked = world.tick(elapsed); !ticked) {
            log::error(k_log_category, "PIE stopped after a simulation error ({})", static_cast<int>(ticked.error()));
            stopPlay();
        }
    }

    void finishTransition() {
        stopPlay();
        reload_status.clear();
        if (quit_requested) {
            quit = true;
        } else {
            document = std::move(next_document);
            rename_target = {};
        }
        confirm_transition = false;
    }

    void requestTransition(std::shared_ptr<EditorDocument> replacement, bool close = false) {
        if (confirm_transition) {
            return;
        }
        quit_requested = close;
        next_document = std::move(replacement);
        save_error.clear();
        if (document && document->isDirty()) {
            confirm_transition = true;
        } else {
            finishTransition();
        }
    }

    void drawUnsavedChanges() {
        constexpr const char *title = "Unsaved Scene";
        if (confirm_transition && !ImGui::IsPopupOpen(title)) {
            ImGui::OpenPopup(title);
        }
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::Text("Save changes to %s?", document->getName().c_str());
        if (!save_error.empty()) {
            ImGui::TextUnformatted(save_error.c_str());
        }
        if (ImGui::Button("Save") && saveDocument()) {
            finishTransition();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            finishTransition();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            next_document.reset();
            confirm_transition = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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

    void sync_name_buffer(const GameObject *selected) {
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
                if (auto created = EditorDocument::createEmpty("Untitled", gameModulePath()); created) {
                    requestTransition(*created);
                } else {
                    log::error(k_log_category, "New scene failed: {}", created.error().message);
                }
            }
            if (ImGui::MenuItem("Save", "Ctrl+S", false, document && !document->isRecovering() && !play_document)) {
                saveDocument();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                requestTransition({}, true);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            auto *active = activeDocument();
            const std::string undo_label =
                active && active->canUndo() ? std::format("Undo {}", active->getUndoName()) : "Undo";
            const std::string redo_label =
                active && active->canRedo() ? std::format("Redo {}", active->getRedoName()) : "Redo";
            if (ImGui::MenuItem(undo_label.c_str(), "Ctrl+Z", false, active && active->canUndo())) {
                if (active->undo()) {
                    rename_target = {};
                }
            }
            if (ImGui::MenuItem(redo_label.c_str(), "Ctrl+Y", false, active && active->canRedo())) {
                if (active->redo()) {
                    rename_target = {};
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Play")) {
            drawPlayControls();
            ImGui::Separator();
            if (ImGui::MenuItem("Reload Game Module", nullptr, false, !gameModulePath().empty())) {
                const auto result = document->reloadGameModule(gameModulePath(), project_root / "Saved" / "Recovery",
                                                               [this] { stopPlay(); });
                rename_target = {};
                if (result) {
                    reload_status.clear();
                    log::info(k_log_category, "Game module reloaded");
                } else {
                    reload_status = result.error().message;
                    log::error(k_log_category, "{}", reload_status);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene", nullptr, &show_viewport, static_cast<bool>(scene_viewport));
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
            if (document->isRecovering()) {
                ImGui::TextColored(ImVec4(0.95F, 0.47F, 0.38F, 1.0F), "Recovery");
            }
            if (play_document) {
                ImGui::TextColored(ImVec4(0.95F, 0.74F, 0.32F, 1.0F), "%s", play_paused ? "Paused" : "Playing");
            }
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
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    void draw_hierarchy() {
        if (!show_hierarchy) {
            return;
        }
        if (!ImGui::Begin("Hierarchy", &show_hierarchy)) {
            ImGui::End();
            return;
        }
        if (auto *active = activeDocument()) {
            if (ImGui::Button("Create Object")) {
                active->execute(std::make_unique<CreateObjectCommand>("Object"));
            }
            ImGui::SameLine();
            const bool has_selection = active->findSelected() != nullptr;
            if (ImGui::Button("Delete") && has_selection) {
                const Guid target = active->getSelection().front();
                active->execute(std::make_unique<DeleteObjectCommand>(target));
                rename_target = {};
            }
            ImGui::Separator();
            Scene &scene = active->getScene();
            std::map<Guid, std::vector<const GameObject *>> children;
            for (const ObjectHandle handle : scene.getObjects()) {
                if (const auto *object = scene.findObject(handle)) {
                    const auto *parent = object->getParent();
                    children[parent ? parent->getGuid() : Guid{}].push_back(object);
                }
            }
            drawHierarchyChildren(*active, children, {});
        } else {
            ImGui::TextDisabled("No scene open");
        }
        ImGui::End();
    }

    void drawHierarchyChildren(EditorDocument &active, const std::map<Guid, std::vector<const GameObject *>> &children,
                               const Guid &parent) {
        const auto siblings = children.find(parent);
        if (siblings == children.end()) {
            return;
        }
        for (const GameObject *object : siblings->second) {
            const Guid guid = object->getGuid();
            const auto &selection = active.getSelection();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!children.contains(guid)) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (std::find(selection.begin(), selection.end(), guid) != selection.end()) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            const std::string id = guid.toString();
            ImGui::PushID(id.c_str());
            const bool expanded = ImGui::TreeNodeEx("object", flags, "%s", object->getName().c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                active.select(guid, ImGui::GetIO().KeyCtrl);
                rename_target = {};
            }
            if (expanded) {
                drawHierarchyChildren(active, children, guid);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
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
            void *member = property.get_mutable ? property.get_mutable(value) : nullptr;
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
                const bool read_only = reflect::hasFlag(property.flags, reflect::PropertyFlags::ReadOnly);
                ImGui::BeginDisabled(read_only);
                if (ImGui::Checkbox(name.c_str(), &current)) {
                    push_property_command(target, object, component, path, current ? "true" : "false");
                }
                ImGui::EndDisabled();
                break;
            }
            case reflect::PropertyKind::FloatingPoint:
            case reflect::PropertyKind::SignedInteger:
            case reflect::PropertyKind::UnsignedInteger:
            case reflect::PropertyKind::String: {
                const std::string current = property.format_scalar ? property.format_scalar(member) : "";
                if (property.kind == reflect::PropertyKind::String ||
                    reflect::hasFlag(property.flags, reflect::PropertyFlags::ReadOnly) || !property.parse_scalar) {
                    ImGui::LabelText(name.c_str(), "%s", current.c_str());
                    break;
                }
                char input[128]{};
                current.copy(input, sizeof(input) - 1);
                if (ImGui::InputText(name.c_str(), input, sizeof(input), ImGuiInputTextFlags_EnterReturnsTrue) &&
                    current != input) {
                    push_property_command(target, object, component, path, input);
                }
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
                        ImGui::BeginDisabled(reflect::hasFlag(property.flags, reflect::PropertyFlags::ReadOnly));
                        draw_properties(target, object, component, *nested, member, path);
                        ImGui::EndDisabled();
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
        auto *active = activeDocument();
        if (!reload_status.empty()) {
            ImGui::TextWrapped("%s", reload_status.c_str());
            ImGui::Separator();
        }
        if (play_document) {
            ImGui::TextWrapped("Runtime changes are discarded when play stops.");
        }
        GameObject *selected = active ? active->findSelected() : nullptr;
        if (selected == nullptr) {
            ImGui::TextDisabled("Select an object in the hierarchy");
            ImGui::End();
            return;
        }
        const auto selection_id = selected->getGuid().toString();
        ImGui::PushID(selection_id.c_str());
        sync_name_buffer(selected);
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::InputText("##name", rename_buffer, sizeof(rename_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            active->execute(std::make_unique<RenameObjectCommand>(selected->getGuid(), rename_buffer));
        }
        ImGui::TextDisabled("Guid %s", selected->getGuid().toString().c_str());
        if (play_document && document->getScene().findObject(selected->getGuid()) && ImGui::Button("Keep Transform")) {
            document->execute(std::make_unique<SetTransformCommand>(selected->getGuid(), selected->getTransform()));
        }

        Scene &scene = active->getScene();
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
                draw_properties(*active, selected->getGuid(), component, *type, value, path);
            }
        }
        ImGui::PopID();
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
            if (scene_viewport) {
                scene_viewport->refreshAssets();
            }
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
                    if (auto opened = EditorDocument::open(scene, gameModulePath()); opened) {
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
            requestTransition(std::move(pending));
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
        ImGui::Text("Window %u x %u", status.width, status.height);
        ImGui::Text("ImGui draw calls %u", status.draw_calls);
        if (const auto *active = activeDocument()) {
            ImGui::Text("Objects %zu", active->getScene().getObjectCount());
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

    void handleShortcuts() {
        if (!ImGui::GetIO().KeyCtrl || ImGui::GetIO().WantTextInput || confirm_transition || !document) {
            return;
        }
        auto *active = activeDocument();
        if (!play_document && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            saveDocument();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && active->undo()) {
            rename_target = {};
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && active->redo()) {
            rename_target = {};
        }
    }

    void draw_frame(const EditorShellStatus &status, double now) {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
        if (!layout_initialized) {
            const auto *node = ImGui::DockBuilderGetNode(dockspace_id);
            if (node == nullptr || !node->IsSplitNode()) {
                build_default_layout(dockspace_id);
            }
            layout_initialized = true;
        }
        if (now >= next_scan) {
            rescan_content();
            next_scan = now + 2.0;
        }
        handleShortcuts();
        draw_menu_bar();
        tickPlay(status.frame_time_ms * 0.001F);
        draw_hierarchy();
        draw_inspector();
        if (scene_viewport && show_viewport) {
            if (const auto *center = ImGui::DockBuilderGetCentralNode(dockspace_id)) {
                ImGui::SetNextWindowDockID(center->ID, ImGuiCond_FirstUseEver);
            }
            if (ImGui::Begin("Scene", &show_viewport,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) &&
                activeDocument()) {
                scene_viewport->draw(*activeDocument(), status.frame_time_ms * 0.001F, play_document && !simulate);
            }
            ImGui::End();
        }
        draw_content();
        draw_console();
        draw_statistics(status);
        drawUnsavedChanges();
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
    if (m_impl->scene_viewport) {
        m_impl->scene_viewport->setProjectRoot(m_impl->project_root);
    }
    m_impl->next_scan = 0.0;
}

const std::filesystem::path &EditorShell::getProjectRoot() const noexcept {
    static const std::filesystem::path empty;
    return m_impl ? m_impl->project_root : empty;
}

void EditorShell::draw(const EditorShellStatus &status) {
    ImGuizmo::BeginFrame();
    m_impl->draw_frame(status, ImGui::GetTime());
}

std::expected<void, UiError> EditorShell::initializeViewport(rhi::Device &device, ImGuiLayer &layer,
                                                             const std::filesystem::path &shader_directory) {
    auto viewport = std::make_unique<SceneViewport>(device, layer);
    if (auto initialized = viewport->initialize(shader_directory); !initialized) {
        return std::unexpected(std::move(initialized.error()));
    }
    viewport->setProjectRoot(m_impl->project_root);
    m_impl->scene_viewport = std::move(viewport);
    return {};
}

void EditorShell::renderViewport(rhi::CommandList &commands) {
    if (m_impl->scene_viewport) {
        m_impl->scene_viewport->render(commands);
    }
}

bool EditorShell::shouldQuit() const noexcept { return !m_impl || m_impl->quit; }

void EditorShell::requestQuit() {
    if (m_impl) {
        m_impl->requestTransition({}, true);
    }
}

} // namespace star::editor
