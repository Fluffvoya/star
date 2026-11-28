#include "scene_viewport.hpp"

#include "core/log.hpp"
#include "editor/command.hpp"
#include "editor/document.hpp"
#include "scene/components.hpp"
#include "scene/world.hpp"

#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <vector>

namespace star::editor {
namespace {

constexpr int k_grid_radius = 20;
constexpr std::size_t k_vertex_count = (2 * k_grid_radius + 1) * 4;
const LogCategory k_viewport_log("SceneViewport", LogLevel::Info);
using MatrixElements = std::array<float, 16>;

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
    m_models = std::make_unique<SceneModels>(m_device, shader_directory);
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
        .depth_test = m_models->isReady(),
        .color_formats = formats,
        .has_depth = m_models->isReady(),
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
        if (m_depth.isValid()) {
            static_cast<void>(m_device.destroy(m_depth));
            m_depth = {};
        }
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
    rhi::TextureHandle depth;
    if (m_models->isReady()) {
        const auto attachment = m_device.createTexture({
            .width = width,
            .height = height,
            .format = rhi::TextureFormat::D24UnormS8Uint,
            .usage = rhi::TextureUsage::DepthStencil,
            .debugName = "Scene Viewport Depth",
        });
        if (!attachment) {
            m_layer.unregisterTexture(*image);
            static_cast<void>(m_device.destroy(*view));
            static_cast<void>(m_device.destroy(*target));
            return false;
        }
        depth = *attachment;
    }
    static_cast<void>(m_device.waitIdle());
    releaseTarget();
    m_target = *target;
    m_depth = depth;
    m_view = *view;
    m_image_id = *image;
    m_width = width;
    m_height = height;
    return true;
}

void SceneViewport::setProjectRoot(const std::filesystem::path &root) { m_models->setProjectRoot(root); }

void SceneViewport::refreshAssets() { m_models->refresh(); }

math::Vec3 SceneViewport::forward() const {
    return {-std::sin(m_yaw) * std::cos(m_pitch), std::sin(m_pitch), -std::cos(m_yaw) * std::cos(m_pitch)};
}

SceneViewport::CameraView SceneViewport::cameraView() const {
    if (m_game_camera) {
        return *m_game_camera;
    }
    const auto direction = forward();
    const auto right = math::normalize(math::cross(direction, {0, 1, 0}));
    return {m_eye, direction, right, math::cross(right, direction), math::pi / 3.0F, 0.05F, 250.0F};
}

math::Mat4::Vector4 SceneViewport::project(math::Vec3 point, float aspect) const {
    const auto camera = cameraView();
    const auto relative = point - camera.eye;
    const float distance = math::dot(relative, camera.forward);
    if (m_orthographic && !m_game_camera) {
        const float scale = 2.0F / m_ortho_height;
        return {math::dot(relative, camera.right) * scale / aspect, -math::dot(relative, camera.up) * scale,
                (distance - camera.near_plane) / (camera.far_plane - camera.near_plane), 1.0F};
    }
    const float scale = 1.0F / std::tan(camera.fov * 0.5F);
    return {math::dot(relative, camera.right) * scale / aspect, -math::dot(relative, camera.up) * scale,
            (distance - camera.near_plane) * camera.far_plane / (camera.far_plane - camera.near_plane), distance};
}

math::Mat4 SceneViewport::clipFromModel(const math::Mat4 &world, float aspect) const {
    const auto camera = cameraView();
    const bool orthographic = m_orthographic && !m_game_camera;
    const float aperture = orthographic ? 2.0F / m_ortho_height : 1.0F / std::tan(camera.fov * 0.5F);
    const float depth_scale = (orthographic ? 1.0F : camera.far_plane) / (camera.far_plane - camera.near_plane);
    math::Mat4 clip;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto &column = world.columns[axis];
        const math::Vec3 direction{column.x, column.y, column.z};
        const auto distance = math::dot(direction, camera.forward);
        clip.columns[axis] = {math::dot(direction, camera.right) * aperture / aspect,
                              -math::dot(direction, camera.up) * aperture, distance * depth_scale,
                              orthographic ? 0.0F : distance};
    }
    clip.columns[3] = project(world.transformPoint({}), aspect);
    return clip;
}

void SceneViewport::cancelGizmo() {
    if (m_drag) {
        m_drag.reset();
        ImGuizmo::Enable(false);
    }
}

bool SceneViewport::drawGizmo(EditorDocument &document, ImVec2 origin, ImVec2 size, bool hovered) {
    auto *selected = document.findSelected();
    if (!selected || m_looking || document.getScene().getWorld().isPlaying()) {
        cancelGizmo();
        return false;
    }
    const auto &original = selected->getWorldTransform().matrix;
    const auto object = selected->getHandle();
    if (m_drag &&
        (m_drag->object != object || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || !ImGui::IsWindowFocused() ||
         std::bit_cast<MatrixElements>(m_drag->original) != std::bit_cast<MatrixElements>(original))) {
        cancelGizmo();
        return true;
    }
    if (hovered && !m_drag && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            m_gizmo_operation = 0;
        } else if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            m_gizmo_operation = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            m_gizmo_operation = 2;
        }
    }
    const auto camera = cameraView();
    const auto &right = camera.right;
    const auto &up = camera.up;
    const auto backward = -camera.forward;
    const MatrixElements view{
        right.x,
        up.x,
        backward.x,
        0.0F,
        right.y,
        up.y,
        backward.y,
        0.0F,
        right.z,
        up.z,
        backward.z,
        0.0F,
        -math::dot(right, camera.eye),
        -math::dot(up, camera.eye),
        -math::dot(backward, camera.eye),
        1.0F,
    };
    const float near_plane = camera.near_plane;
    const float far_plane = camera.far_plane;
    const float depth = far_plane - near_plane;
    const float vertical = m_orthographic ? 2.0F / m_ortho_height : 1.0F / std::tan(camera.fov * 0.5F);
    const MatrixElements projection{
        vertical * size.y / size.x,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        vertical,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        m_orthographic ? -2.0F / depth : -(far_plane + near_plane) / depth,
        m_orthographic ? 0.0F : -1.0F,
        0.0F,
        0.0F,
        m_orthographic ? -(far_plane + near_plane) / depth : -2.0F * far_plane * near_plane / depth,
        m_orthographic ? 1.0F : 0.0F,
    };
    auto matrix = std::bit_cast<MatrixElements>(m_drag ? m_drag->preview : original);
    constexpr std::array operations{ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    constexpr std::array increments{0.5F, 15.0F, 0.1F};
    const auto operation = static_cast<std::size_t>(m_gizmo_operation);
    const std::array snap{increments[operation], increments[operation], increments[operation]};
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::SetOrthographic(m_orthographic);
    ImGuizmo::Enable(hovered || m_drag.has_value());
    const bool changed = ImGuizmo::Manipulate(view.data(), projection.data(), operations[operation],
                                              m_local_gizmo ? ImGuizmo::LOCAL : ImGuizmo::WORLD, matrix.data(), nullptr,
                                              m_snap_gizmo ? snap.data() : nullptr);
    const bool using_gizmo = ImGuizmo::IsUsing();
    const bool was_dragging = m_drag.has_value();
    if (using_gizmo && !m_drag) {
        m_drag = TransformDrag{object, original, original, {}};
    }
    if (changed && m_drag) {
        m_drag->preview = std::bit_cast<math::Mat4>(matrix);
        const auto converted = document.getScene().localTransformForWorld(object, m_drag->preview);
        m_drag->local = converted ? std::optional{*converted} : std::nullopt;
    }
    if (was_dragging && !using_gizmo) {
        if (m_drag->local &&
            std::bit_cast<MatrixElements>(original) != std::bit_cast<MatrixElements>(m_drag->preview)) {
            document.execute(std::make_unique<SetTransformCommand>(selected->getGuid(), *m_drag->local));
        } else if (!m_drag->local &&
                   std::bit_cast<MatrixElements>(original) != std::bit_cast<MatrixElements>(m_drag->preview)) {
            log::warning(k_viewport_log, "Transform cannot be represented relative to the parent");
        }
        m_drag.reset();
    }
    return was_dragging || using_gizmo || ImGuizmo::IsOver();
}

void SceneViewport::draw(EditorDocument &document, float delta_seconds, bool game_view) {
    Scene &scene = document.getScene();
    if (!scene.getWorld().isPlaying() && !scene.commitCommands()) {
        return;
    }
    const auto objects = scene.getObjects();
    m_game_camera.reset();
    if (game_view) {
        cancelGizmo();
        m_looking = false;
        for (const auto handle : objects) {
            const auto *object = scene.findObject(handle);
            const auto *camera = scene.findComponent<Camera>(handle);
            if (!object || !object->isActiveInHierarchy() || !camera || !std::isfinite(camera->vertical_fov) ||
                camera->vertical_fov <= 0.0F || camera->vertical_fov >= math::pi ||
                !std::isfinite(camera->near_plane) || !std::isfinite(camera->far_plane) || camera->near_plane <= 0.0F ||
                camera->far_plane <= camera->near_plane) {
                continue;
            }
            const auto &matrix = object->getWorldTransform().matrix;
            const auto &z = matrix.columns[2];
            const auto &y = matrix.columns[1];
            const auto direction = math::normalize(math::Vec3{-z.x, -z.y, -z.z});
            const auto right = math::normalize(math::cross(direction, {y.x, y.y, y.z}));
            if (!(math::lengthSquared(right) > 0.0F)) {
                continue;
            }
            m_game_camera = CameraView{
                matrix.transformPoint({}), direction,        right, math::cross(right, direction), camera->vertical_fov,
                camera->near_plane,        camera->far_plane};
            break;
        }
        if (!m_game_camera) {
            ImGui::TextDisabled("No active game camera. Use Play > Simulation Camera to inspect the scene.");
            return;
        }
        ImGui::TextDisabled("Game Camera");
    } else {
        if (!scene.getWorld().isPlaying()) {
            constexpr const char *operations[] = {"Move (W)", "Rotate (E)", "Scale (R)"};
            ImGui::BeginDisabled(m_drag.has_value());
            ImGui::SetNextItemWidth(115.0F);
            ImGui::Combo("##transform-operation", &m_gizmo_operation, operations, IM_ARRAYSIZE(operations));
            ImGui::SameLine();
            ImGui::Checkbox("Local", &m_local_gizmo);
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &m_snap_gizmo);
            ImGui::EndDisabled();
        }
        ImGui::Checkbox("Orthographic", &m_orthographic);
        ImGui::SameLine();
        ImGui::TextDisabled("RMB + WASD to fly  |  F to focus");
    }
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 2.0F || size.y < 2.0F ||
        !resize(static_cast<std::uint32_t>(size.x), static_cast<std::uint32_t>(size.y))) {
        m_looking = false;
        cancelGizmo();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(m_image_id), size);
    m_draw_requested = true;
    m_draw_grid = !game_view;
    m_model_instances.clear();
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO &io = ImGui::GetIO();
    if (!game_view && !m_drag && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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
    if (!game_view && !m_drag && hovered && io.MouseWheel != 0.0F) {
        if (m_orthographic) {
            m_ortho_height = std::clamp(m_ortho_height * std::pow(0.8F, io.MouseWheel), 0.25F, 150.0F);
        } else {
            m_eye = m_eye + forward() * io.MouseWheel;
        }
    }
    if (!game_view && !m_drag && hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        if (const auto *selected = document.findSelected()) {
            m_eye = selected->getWorldTransform().matrix.transformPoint({}) - forward() * 6.0F;
        }
    }
    auto *draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const bool gizmo_captured = !game_view && drawGizmo(document, origin, size, hovered);
    const auto &selection = document.getSelection();
    const float aspect = size.x / size.y;
    Guid picked;
    float closest = 100.0F;
    float picked_depth = std::numeric_limits<float>::infinity();
    for (const auto handle : objects) {
        const auto *object = scene.findObject(handle);
        if (object == nullptr) {
            continue;
        }
        const auto &matrix = m_drag && m_drag->object == handle ? m_drag->preview : object->getWorldTransform().matrix;
        if (m_models->isReady() && object->isActiveInHierarchy()) {
            if (const auto *mesh = scene.findComponent<MeshRenderer>(handle);
                mesh && mesh->mesh.isValid() && mesh->layer_mask != 0) {
                m_model_instances.push_back({mesh->mesh, clipFromModel(matrix, aspect)});
            }
        }
        if (game_view) {
            continue;
        }
        const auto clip = project(matrix.transformPoint({}), aspect);
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
    if (!game_view && !gizmo_captured && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
    const bool uploaded = m_draw_grid && m_device.writeBuffer(m_vertices, std::as_bytes(std::span(vertices)));
    commands.beginRenderPass({
        .color_target = m_target,
        .clear_color = {0.08F, 0.09F, 0.11F, 1.0F},
        .debugName = "Scene Viewport",
        .depth_target = m_depth,
    });
    m_models->draw(commands, m_model_instances);
    if (uploaded) {
        commands.bindGraphicsPipeline(m_pipeline, {});
        commands.bindVertexBuffer(m_vertices);
        commands.draw(static_cast<std::uint32_t>(vertices.size()));
    }
    commands.endRenderPass();
}

} // namespace star::editor
