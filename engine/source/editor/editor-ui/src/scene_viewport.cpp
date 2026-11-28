#include "scene_viewport.hpp"

#include "editor/document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>

namespace star::editor {
namespace {

constexpr int k_grid_radius = 20;
constexpr std::size_t k_vertex_count = (2 * k_grid_radius + 1) * 4;

[[nodiscard]] std::vector<std::uint32_t> readSpirv(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size % sizeof(std::uint32_t) != 0) {
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(words.data()), size)) {
        return {};
    }
    return words;
}

} // namespace

SceneViewport::SceneViewport(rhi::Device &device, ImGuiLayer &layer) : m_device(device), m_layer(layer) {}

SceneViewport::~SceneViewport() {
    static_cast<void>(m_device.waitIdle());
    releaseTarget();
    static_cast<void>(m_device.destroy(m_vertices));
    static_cast<void>(m_device.destroy(m_pipeline));
    static_cast<void>(m_device.destroy(m_layout));
    static_cast<void>(m_device.destroy(m_fragment_shader));
    static_cast<void>(m_device.destroy(m_vertex_shader));
}

std::expected<void, UiError> SceneViewport::initialize(const std::filesystem::path &shader_directory) {
    const auto vertex = readSpirv(shader_directory / "scene-viewport-vertex.spv");
    const auto fragment = readSpirv(shader_directory / "scene-viewport-fragment.spv");
    const auto vertex_shader = m_device.createShaderModule({rhi::ShaderStage::Vertex, vertex, "Viewport Vertex"});
    if (!vertex_shader) {
        return std::unexpected(UiError{"Cannot load the scene viewport vertex shader"});
    }
    m_vertex_shader = *vertex_shader;
    const auto fragment_shader =
        m_device.createShaderModule({rhi::ShaderStage::Fragment, fragment, "Viewport Fragment"});
    if (!fragment_shader) {
        return std::unexpected(UiError{"Cannot load the scene viewport fragment shader"});
    }
    m_fragment_shader = *fragment_shader;
    const auto layout = m_device.createPipelineLayout({.bind_groups = {}, .debugName = "Viewport Layout"});
    if (!layout) {
        return std::unexpected(UiError{"Cannot create the scene viewport pipeline layout"});
    }
    m_layout = *layout;
    const std::array attributes{
        rhi::VertexAttribute{0, rhi::VertexFormat::Float4, offsetof(Vertex, position)},
        rhi::VertexAttribute{1, rhi::VertexFormat::Float3, offsetof(Vertex, color)},
    };
    const std::array vertex_layout{rhi::VertexBufferLayout{sizeof(Vertex), attributes}};
    const std::array formats{rhi::TextureFormat::Rgba8Unorm};
    const auto pipeline = m_device.createGraphicsPipeline({
        .layout = m_layout,
        .vertex_shader = m_vertex_shader,
        .fragment_shader = m_fragment_shader,
        .vertex_buffers = vertex_layout,
        .topology = rhi::PrimitiveTopology::LineList,
        .color_formats = formats,
        .debugName = "Viewport Grid",
    });
    if (!pipeline) {
        return std::unexpected(UiError{"Cannot create the scene viewport grid pipeline"});
    }
    m_pipeline = *pipeline;
    const auto vertices = m_device.createBuffer({
        .size = sizeof(Vertex) * k_vertex_count,
        .usage = rhi::BufferUsage::Vertex,
        .memory = rhi::MemoryType::Upload,
        .debugName = "Viewport Grid Vertices",
    });
    if (!vertices) {
        return std::unexpected(UiError{"Cannot allocate the scene viewport grid buffer"});
    }
    m_vertices = *vertices;
    return {};
}

void SceneViewport::releaseTarget() {
    if (m_image_id != 0) {
        m_layer.unregisterTexture(m_image_id);
        static_cast<void>(m_device.destroy(m_view));
        static_cast<void>(m_device.destroy(m_target));
        m_image_id = 0;
    }
}

bool SceneViewport::resize(std::uint32_t width, std::uint32_t height) {
    if (m_image_id != 0 && width == m_width && height == m_height) {
        return true;
    }
    const auto target = m_device.createTexture({
        .width = width,
        .height = height,
        .format = rhi::TextureFormat::Rgba8Unorm,
        .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
        .debugName = "Scene Viewport",
    });
    if (!target) {
        return false;
    }
    const auto view = m_device.createTextureView({.texture = *target, .debugName = "Scene Viewport View"});
    if (!view) {
        static_cast<void>(m_device.destroy(*target));
        return false;
    }
    const auto image = m_layer.registerTexture(*view);
    if (!image) {
        static_cast<void>(m_device.destroy(*view));
        static_cast<void>(m_device.destroy(*target));
        return false;
    }
    static_cast<void>(m_device.waitIdle());
    releaseTarget();
    m_target = *target;
    m_view = *view;
    m_image_id = *image;
    m_width = width;
    m_height = height;
    return true;
}

math::Vec3 SceneViewport::forward() const {
    return {-std::sin(m_yaw) * std::cos(m_pitch), std::sin(m_pitch), -std::cos(m_yaw) * std::cos(m_pitch)};
}

math::Mat4::Vector4 SceneViewport::project(math::Vec3 point, float aspect) const {
    const auto direction = forward();
    const auto right = math::normalize(math::cross(direction, {0, 1, 0}));
    const auto up = math::cross(right, direction);
    const auto relative = point - m_eye;
    const float distance = math::dot(relative, direction);
    constexpr float near_plane = 0.05F;
    constexpr float far_plane = 250.0F;
    if (m_orthographic) {
        const float scale = 2.0F / m_ortho_height;
        return {math::dot(relative, right) * scale / aspect, -math::dot(relative, up) * scale,
                (distance - near_plane) / (far_plane - near_plane), 1.0F};
    }
    const float scale = 1.0F / std::tan(math::pi / 6.0F);
    return {math::dot(relative, right) * scale / aspect, -math::dot(relative, up) * scale,
            (distance - near_plane) * far_plane / (far_plane - near_plane), distance};
}

void SceneViewport::draw(EditorDocument &document, float delta_seconds) {
    ImGui::Checkbox("Orthographic", &m_orthographic);
    ImGui::SameLine();
    ImGui::TextDisabled("RMB + WASD to fly  |  F to focus");
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 2.0F || size.y < 2.0F ||
        !resize(static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y))) {
        m_looking = false;
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(m_image_id), size);
    m_draw_requested = true;
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO &io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_looking = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || !ImGui::IsWindowFocused()) {
        m_looking = false;
    }
    if (m_looking) {
        m_yaw -= io.MouseDelta.x * 0.004F;
        m_pitch = std::clamp(m_pitch - io.MouseDelta.y * 0.004F, -1.52F, 1.52F);
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
            return static_cast<float>(ImGui::IsKeyDown(positive)) - static_cast<float>(ImGui::IsKeyDown(negative));
        };
        const auto direction = forward();
        const auto right = math::normalize(math::cross(direction, {0, 1, 0}));
        const auto movement = direction * axis(ImGuiKey_W, ImGuiKey_S) + right * axis(ImGuiKey_D, ImGuiKey_A) +
                              math::Vec3{0, 1, 0} * axis(ImGuiKey_E, ImGuiKey_Q);
        m_eye = m_eye + math::normalize(movement) * (std::min(delta_seconds, 0.1F) * (io.KeyShift ? 15.0F : 5.0F));
    }
    if (hovered && io.MouseWheel != 0.0F) {
        if (m_orthographic) {
            m_ortho_height = std::clamp(m_ortho_height * std::pow(0.8F, io.MouseWheel), 0.25F, 150.0F);
        } else {
            m_eye = m_eye + forward() * io.MouseWheel;
        }
    }
    Scene &scene = document.getScene();
    if (!scene.commitCommands()) {
        return;
    }
    if (hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        if (const auto *selected = document.findSelected()) {
            m_eye = selected->getWorldTransform().matrix.transformPoint({}) - forward() * 6.0F;
        }
    }
    auto *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const auto &selection = document.getSelection();
    const float aspect = size.x / size.y;
    Guid picked;
    float closest = 100.0F;
    float picked_depth = std::numeric_limits<float>::infinity();
    for (const auto handle : scene.getObjects()) {
        const auto *object = scene.findObject(handle);
        if (object == nullptr) {
            continue;
        }
        const auto clip = project(object->getWorldTransform().matrix.transformPoint({}), aspect);
        if (clip.w <= 0.0F || clip.z < 0.0F || clip.z > clip.w || std::abs(clip.x) > clip.w ||
            std::abs(clip.y) > clip.w) {
            continue;
        }
        const ImVec2 point{origin.x + (0.5F + 0.5F * clip.x / clip.w) * size.x,
                           origin.y + (0.5F + 0.5F * clip.y / clip.w) * size.y};
        const bool selected = std::find(selection.begin(), selection.end(), object->getGuid()) != selection.end();
        const ImU32 color = selected ? IM_COL32(255, 191, 89, 255) : IM_COL32(159, 181, 205, 255);
        draw_list->AddCircleFilled(point, selected ? 5.0F : 3.0F, color);
        if (selected) {
            draw_list->AddText({point.x + 8.0F, point.y - 8.0F}, color, object->getName().c_str());
        }
        const float distance = math::lengthSquared(math::Vec2{point.x - io.MousePos.x, point.y - io.MousePos.y});
        const float depth = clip.z / clip.w;
        if (distance < closest || (distance == closest && depth < picked_depth)) {
            picked = object->getGuid();
            closest = distance;
            picked_depth = depth;
        }
    }
    draw_list->PopClipRect();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (picked.isValid()) {
            document.select(picked, io.KeyCtrl);
        } else if (!io.KeyCtrl) {
            document.clearSelection();
        }
    }
}

void SceneViewport::render(rhi::CommandList &commands) {
    if (!std::exchange(m_draw_requested, false)) {
        return;
    }
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    std::array<Vertex, k_vertex_count> vertices;
    std::size_t next = 0;
    const auto appendLine = [&](math::Vec3 from, math::Vec3 to, std::array<float, 3> color) {
        vertices[next++] = {project(from, aspect), color};
        vertices[next++] = {project(to, aspect), color};
    };
    constexpr float extent = static_cast<float>(k_grid_radius);
    for (int line = -k_grid_radius; line <= k_grid_radius; ++line) {
        const float offset = static_cast<float>(line);
        const float shade = line % 5 == 0 ? 0.24F : 0.15F;
        const std::array gray{shade, shade, shade + 0.025F};
        appendLine({-extent, 0, offset}, {extent, 0, offset}, line == 0 ? std::array{0.48F, 0.20F, 0.21F} : gray);
        appendLine({offset, 0, -extent}, {offset, 0, extent}, line == 0 ? std::array{0.20F, 0.35F, 0.53F} : gray);
    }
    const auto uploaded = m_device.writeBuffer(m_vertices, std::as_bytes(std::span(vertices)));
    commands.beginRenderPass({
        .color_target = m_target,
        .clear_color = {0.08F, 0.09F, 0.11F, 1.0F},
        .debugName = "Scene Viewport",
    });
    if (uploaded) {
        commands.bindGraphicsPipeline(m_pipeline, {});
        commands.bindVertexBuffer(m_vertices);
        commands.draw(static_cast<std::uint32_t>(vertices.size()));
    }
    commands.endRenderPass();
}

} // namespace star::editor
