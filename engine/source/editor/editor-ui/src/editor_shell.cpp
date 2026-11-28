#include "editor-ui/editor_shell.hpp"

#include "core/log.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
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
    bool layout_initialized = false;
    bool show_console = true;
    bool show_stats = true;
    bool show_demo = false;
    bool auto_scroll = true;
    LogLevel minimum_level = LogLevel::Info;
    log::LogSinkId sink = 0;
    std::mutex mutex;
    std::deque<ConsoleEntry> entries;

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

    void draw_menu_bar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit", "Alt+F4")) {
                quit = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
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
        ImGui::EndMainMenuBar();
    }

    void build_default_layout(ImGuiID dockspace_id) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID center = dockspace_id;
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30F, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28F, nullptr, &center);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Statistics", right);
        ImGui::DockBuilderFinish(dockspace_id);
        layout_initialized = true;
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
        ImGui::SetNextWindowSize(ImVec2(320.0F, 220.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Statistics", &show_stats)) {
            ImGui::End();
            return;
        }
        ImGui::Text("Frame %.2f ms", static_cast<double>(status.frame_time_ms));
        ImGui::Text("FPS %.1f", static_cast<double>(status.fps));
        ImGui::Separator();
        ImGui::Text("Viewport %u x %u", status.width, status.height);
        ImGui::Text("ImGui draw calls %u", status.draw_calls);
        if (!status.device_name.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("GPU %.*s", static_cast<int>(status.device_name.size()), status.device_name.data());
        }
        ImGui::Separator();
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::Text("Renderer: star-rhi");
        ImGui::End();
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

void EditorShell::draw(const EditorShellStatus &status) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    if (!m_impl->layout_initialized) {
        m_impl->build_default_layout(dockspace_id);
    }
    m_impl->draw_menu_bar();
    m_impl->draw_console();
    m_impl->draw_statistics(status);
    if (m_impl->show_demo) {
        ImGui::ShowDemoWindow(&m_impl->show_demo);
    }
}

bool EditorShell::shouldQuit() const noexcept { return !m_impl || m_impl->quit; }

void EditorShell::requestQuit() noexcept {
    if (m_impl) {
        m_impl->quit = true;
    }
}

} // namespace star::editor
