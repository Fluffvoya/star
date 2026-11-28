#pragma once

#include "core/math.hpp"
#include "editor-ui/imgui_layer.hpp"

#include <imgui.h>

#include <optional>

namespace star::editor {

class EditorDocument;

class SceneViewport final {
  public:
    SceneViewport(rhi::Device &device, ImGuiLayer &layer);
    ~SceneViewport();
    SceneViewport(const SceneViewport &) = delete;
    SceneViewport &operator=(const SceneViewport &) = delete;

    [[nodiscard]] std::expected<void, UiError> initialize(const std::filesystem::path &shader_directory);
    void draw(EditorDocument &document, float delta_seconds, bool game_view);
    void render(rhi::CommandList &commands);

  private:
    struct Vertex {
        math::Mat4::Vector4 position;
        std::array<float, 3> color;
    };
    struct CameraView {
        math::Vec3 eye;
        math::Vec3 forward;
        math::Vec3 right;
        math::Vec3 up;
        float fov;
        float near_plane;
        float far_plane;
    };

    [[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height);
    void releaseTarget();
    [[nodiscard]] math::Vec3 forward() const;
    [[nodiscard]] math::Mat4::Vector4 project(math::Vec3 point, float aspect) const;

    rhi::Device &m_device;
    ImGuiLayer &m_layer;
    rhi::ShaderModuleHandle m_vertex_shader;
    rhi::ShaderModuleHandle m_fragment_shader;
    rhi::PipelineLayoutHandle m_layout;
    rhi::GraphicsPipelineHandle m_pipeline;
    rhi::BufferHandle m_vertices;
    rhi::TextureHandle m_target;
    rhi::TextureViewHandle m_view;
    std::uint64_t m_image_id = 0;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    bool m_draw_requested = false;
    bool m_looking = false;
    bool m_orthographic = false;
    std::optional<CameraView> m_game_camera;
    float m_ortho_height = 12.0F;
    float m_yaw = 0.6F;
    float m_pitch = -0.45F;
    math::Vec3 m_eye{6.0F, 5.0F, 9.0F};
};

} // namespace star::editor
