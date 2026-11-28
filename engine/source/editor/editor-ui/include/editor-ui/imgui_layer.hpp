#pragma once

#include "editor-ui/export.hpp"
#include "platform/window.hpp"
#include "rhi/rhi.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace star::editor {

struct UiError {
    std::string message;
};

struct ImGuiLayerOptions {
    rhi::Device *device = nullptr;
    rhi::TextureFormat color_format = rhi::TextureFormat::Bgra8Unorm;
    // Directory holding imgui-ui-vertex.spv and imgui-ui-fragment.spv.
    std::filesystem::path shader_directory;
    bool enable_docking = true;
    float font_size = 16.0F;
};

// Owns the ImGui context and records its draw data through the engine RHI.
// Call on the thread that owns the device.
class STAR_EDITORUI_API ImGuiLayer final {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<ImGuiLayer>, UiError> create(const ImGuiLayerOptions &options);

    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer &) = delete;
    ImGuiLayer &operator=(const ImGuiLayer &) = delete;

    // Forwards a platform event. Text input arrives as WindowEventType::TextInput.
    void processEvent(const WindowEvent &event);
    // Sets display size, delta time and starts a new ImGui frame.
    void beginFrame(std::uint32_t width, std::uint32_t height, float delta_seconds);
    // Finishes the ImGui frame. Draw data stays valid until the next beginFrame.
    void endFrame();
    // Records the finished frame into one render pass. Clears the target when clear is true.
    // May be called more than once to render the same frame into several targets.
    void render(rhi::CommandList &commands, rhi::TextureHandle target, bool present_after,
                const std::array<float, 4> &clear_color);
    [[nodiscard]] std::uint32_t getDrawListCount() const noexcept;
    [[nodiscard]] std::uint32_t getDrawCallCount() const noexcept;

  private:
    struct Impl;
    explicit ImGuiLayer(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace star::editor
