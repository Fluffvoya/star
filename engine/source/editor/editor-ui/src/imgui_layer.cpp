#include "editor-ui/imgui_layer.hpp"

#include "core/log.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace star::editor {
namespace {

const LogCategory k_log_category("EditorUI", LogLevel::Info);

struct UiVertex {
    float position[2];
    float uv[2];
    std::uint32_t color;
};

struct UiUniforms {
    float scale[2];
    float translate[2];
};

static_assert(sizeof(UiVertex) == 20, "ImGui vertex layout must match the pipeline description");
static_assert(sizeof(UiUniforms) == 16, "Uniform size must match the shader constant buffer");

[[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        return {};
    }
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

[[nodiscard]] ImGuiKey key_from_scancode(std::uint32_t scancode) {
    switch (scancode) {
    case SDL_SCANCODE_TAB:
        return ImGuiKey_Tab;
    case SDL_SCANCODE_LEFT:
        return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT:
        return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP:
        return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN:
        return ImGuiKey_DownArrow;
    case SDL_SCANCODE_PAGEUP:
        return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return ImGuiKey_PageDown;
    case SDL_SCANCODE_HOME:
        return ImGuiKey_Home;
    case SDL_SCANCODE_END:
        return ImGuiKey_End;
    case SDL_SCANCODE_INSERT:
        return ImGuiKey_Insert;
    case SDL_SCANCODE_DELETE:
        return ImGuiKey_Delete;
    case SDL_SCANCODE_BACKSPACE:
        return ImGuiKey_Backspace;
    case SDL_SCANCODE_SPACE:
        return ImGuiKey_Space;
    case SDL_SCANCODE_RETURN:
        return ImGuiKey_Enter;
    case SDL_SCANCODE_ESCAPE:
        return ImGuiKey_Escape;
    case SDL_SCANCODE_A:
        return ImGuiKey_A;
    case SDL_SCANCODE_C:
        return ImGuiKey_C;
    case SDL_SCANCODE_D:
        return ImGuiKey_D;
    case SDL_SCANCODE_E:
        return ImGuiKey_E;
    case SDL_SCANCODE_F:
        return ImGuiKey_F;
    case SDL_SCANCODE_Q:
        return ImGuiKey_Q;
    case SDL_SCANCODE_R:
        return ImGuiKey_R;
    case SDL_SCANCODE_S:
        return ImGuiKey_S;
    case SDL_SCANCODE_V:
        return ImGuiKey_V;
    case SDL_SCANCODE_W:
        return ImGuiKey_W;
    case SDL_SCANCODE_X:
        return ImGuiKey_X;
    case SDL_SCANCODE_Y:
        return ImGuiKey_Y;
    case SDL_SCANCODE_Z:
        return ImGuiKey_Z;
    case SDL_SCANCODE_F1:
        return ImGuiKey_F1;
    case SDL_SCANCODE_F2:
        return ImGuiKey_F2;
    case SDL_SCANCODE_F3:
        return ImGuiKey_F3;
    case SDL_SCANCODE_F4:
        return ImGuiKey_F4;
    case SDL_SCANCODE_F5:
        return ImGuiKey_F5;
    case SDL_SCANCODE_F6:
        return ImGuiKey_F6;
    case SDL_SCANCODE_F7:
        return ImGuiKey_F7;
    case SDL_SCANCODE_F8:
        return ImGuiKey_F8;
    case SDL_SCANCODE_F9:
        return ImGuiKey_F9;
    case SDL_SCANCODE_F10:
        return ImGuiKey_F10;
    case SDL_SCANCODE_F11:
        return ImGuiKey_F11;
    case SDL_SCANCODE_F12:
        return ImGuiKey_F12;
    default:
        return ImGuiKey_None;
    }
}

[[nodiscard]] ImGuiKey key_from_number_scancode(std::uint32_t scancode) {
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return static_cast<ImGuiKey>(ImGuiKey_1 + (scancode - SDL_SCANCODE_1));
    }
    if (scancode == SDL_SCANCODE_0) {
        return ImGuiKey_0;
    }
    return ImGuiKey_None;
}

[[nodiscard]] ImGuiKey resolve_key(std::uint32_t scancode) {
    const ImGuiKey letter = key_from_scancode(scancode);
    if (letter != ImGuiKey_None) {
        return letter;
    }
    return key_from_number_scancode(scancode);
}

[[nodiscard]] int resolve_mouse_button(std::uint32_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    default:
        return -1;
    }
}

[[nodiscard]] std::string locate_cjk_font() {
    constexpr std::array<const char *, 4> candidates{
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "C:/Windows/Fonts/msyh.ttc",
    };
    for (const char *path : candidates) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) {
            return path;
        }
    }
    return {};
}

} // namespace

struct ImGuiLayer::Impl {
    struct PlatformSwapchain {
        rhi::SwapchainHandle handle;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    rhi::Device *device = nullptr;
    rhi::TextureFormat color_format = rhi::TextureFormat::Bgra8Unorm;
    bool sdl_backend = false;
    rhi::SamplerHandle sampler;
    rhi::ShaderModuleHandle vertex_shader;
    rhi::ShaderModuleHandle fragment_shader;
    rhi::BindGroupLayoutHandle bind_group_layout;
    rhi::PipelineLayoutHandle pipeline_layout;
    rhi::GraphicsPipelineHandle pipeline;
    rhi::TextureHandle font_texture;
    rhi::TextureViewHandle font_view;
    rhi::BufferHandle uniform_buffer;
    rhi::BindGroupHandle bind_group;
    std::map<std::uint64_t, rhi::BindGroupHandle> images;
    std::uint64_t next_image_id = 2;
    rhi::BufferHandle vertex_buffer;
    rhi::BufferHandle index_buffer;
    std::uint64_t vertex_capacity = 0;
    std::uint64_t index_capacity = 0;
    std::string ini_path;
    std::uint32_t draw_list_count = 0;
    std::uint32_t draw_call_count = 0;

    void recordDrawData(rhi::CommandList &commands, ImDrawData *draw_data, rhi::TextureHandle target,
                        bool present_after, const std::array<float, 4> &clear_color);
    static void createPlatformWindow(ImGuiViewport *viewport);
    static void destroyPlatformWindow(ImGuiViewport *viewport);
    static void renderPlatformWindow(ImGuiViewport *viewport, void *);

    [[nodiscard]] bool ensure_buffer(rhi::BufferHandle &buffer, std::uint64_t &capacity, std::uint64_t needed,
                                     rhi::BufferUsage usage, const char *name) {
        if (capacity >= needed && static_cast<bool>(device->isAlive(buffer))) {
            return true;
        }
        if (static_cast<bool>(device->isAlive(buffer))) {
            static_cast<void>(device->destroy(buffer));
        }
        capacity = std::max<std::uint64_t>(needed, 64 * 1024);
        auto created = device->createBuffer(rhi::BufferDesc{
            .size = capacity,
            .usage = usage | rhi::BufferUsage::CopyDestination,
            .memory = rhi::MemoryType::Upload,
            .debugName = name,
        });
        if (!created) {
            capacity = 0;
            buffer = {};
            return false;
        }
        buffer = *created;
        return true;
    }
};

std::expected<std::unique_ptr<ImGuiLayer>, UiError> ImGuiLayer::create(const ImGuiLayerOptions &options) {
    if (options.device == nullptr) {
        return std::unexpected(UiError{"A device is required"});
    }
    auto implementation = std::make_unique<Impl>();
    implementation->device = options.device;
    implementation->color_format = options.color_format;

    const std::vector<std::uint32_t> vertex_spirv = load_spirv(options.shader_directory / "imgui-ui-vertex.spv");
    const std::vector<std::uint32_t> fragment_spirv = load_spirv(options.shader_directory / "imgui-ui-fragment.spv");
    if (vertex_spirv.empty() || fragment_spirv.empty()) {
        return std::unexpected(UiError{"Missing ImGui shaders in " + options.shader_directory.string()});
    }

    auto vertex_shader = options.device->createShaderModule(rhi::ShaderModuleDesc{
        .stage = rhi::ShaderStage::Vertex,
        .spirv = vertex_spirv,
        .debugName = "ImGui Vertex",
    });
    auto fragment_shader = options.device->createShaderModule(rhi::ShaderModuleDesc{
        .stage = rhi::ShaderStage::Fragment,
        .spirv = fragment_spirv,
        .debugName = "ImGui Fragment",
    });
    if (!vertex_shader || !fragment_shader) {
        return std::unexpected(UiError{"Failed to create ImGui shader modules"});
    }
    implementation->vertex_shader = *vertex_shader;
    implementation->fragment_shader = *fragment_shader;

    const std::array layout_entries{
        rhi::BindGroupLayoutEntry{0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex},
        rhi::BindGroupLayoutEntry{1, rhi::BindingType::CombinedImageSampler, rhi::ShaderStage::Fragment},
    };
    auto bind_group_layout =
        options.device->createBindGroupLayout({.entries = layout_entries, .debugName = "ImGui Bind Group Layout"});
    if (!bind_group_layout) {
        return std::unexpected(UiError{"Failed to create the ImGui bind group layout"});
    }
    implementation->bind_group_layout = *bind_group_layout;

    const std::array groups{*bind_group_layout};
    auto pipeline_layout = options.device->createPipelineLayout(
        {.bind_groups = groups, .push_constant_size = 0, .debugName = "ImGui Pipeline Layout"});
    if (!pipeline_layout) {
        return std::unexpected(UiError{"Failed to create the ImGui pipeline layout"});
    }
    implementation->pipeline_layout = *pipeline_layout;

    const std::array attributes{
        rhi::VertexAttribute{0, rhi::VertexFormat::Float2, 0},
        rhi::VertexAttribute{1, rhi::VertexFormat::Float2, 8},
        rhi::VertexAttribute{2, rhi::VertexFormat::UByte4Norm, 16},
    };
    const std::array vertex_layout{rhi::VertexBufferLayout{sizeof(UiVertex), attributes}};
    const std::array color_formats{options.color_format};
    auto pipeline = options.device->createGraphicsPipeline(rhi::GraphicsPipelineDesc{
        .layout = *pipeline_layout,
        .vertex_shader = *vertex_shader,
        .fragment_shader = *fragment_shader,
        .vertex_buffers = vertex_layout,
        .topology = rhi::PrimitiveTopology::TriangleList,
        .cull = rhi::CullMode::None,
        .blend = rhi::BlendMode::Alpha,
        .color_formats = color_formats,
        .debugName = "ImGui Pipeline",
    });
    if (!pipeline) {
        return std::unexpected(UiError{"Failed to create the ImGui pipeline"});
    }
    implementation->pipeline = *pipeline;

    auto sampler = options.device->createSampler(rhi::SamplerDesc{
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .debugName = "ImGui Font Sampler",
    });
    if (!sampler) {
        return std::unexpected(UiError{"Failed to create the ImGui sampler"});
    }
    implementation->sampler = *sampler;

    auto uniform_buffer = options.device->createBuffer(rhi::BufferDesc{
        .size = sizeof(UiUniforms),
        .usage = rhi::BufferUsage::Constant | rhi::BufferUsage::CopyDestination,
        .memory = rhi::MemoryType::Upload,
        .debugName = "ImGui Uniforms",
    });
    if (!uniform_buffer) {
        return std::unexpected(UiError{"Failed to create the ImGui uniform buffer"});
    }
    implementation->uniform_buffer = *uniform_buffer;

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "star-platform";
    io.BackendRendererName = "star-rhi";
    if (options.enable_docking) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    std::error_code error;
    if (std::filesystem::create_directories("Saved/Editor", error) || !error) {
        implementation->ini_path = "Saved/Editor/Layout.ini";
        io.IniFilename = implementation->ini_path.c_str();
    } else {
        io.IniFilename = nullptr;
    }
    ImGui::StyleColorsDark();

    const std::string font_path = locate_cjk_font();
    if (!font_path.empty()) {
        ImFontConfig config;
        config.OversampleH = 2;
        if (io.Fonts->AddFontFromFileTTF(font_path.c_str(), options.font_size, &config,
                                         io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) == nullptr) {
            io.Fonts->AddFontDefault();
        }
    } else {
        io.Fonts->AddFontDefault();
    }

    unsigned char *pixels = nullptr;
    int atlas_width = 0;
    int atlas_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
    if (pixels == nullptr || atlas_width <= 0 || atlas_height <= 0) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to build the ImGui font atlas"});
    }
    auto font_texture = options.device->createTexture(rhi::TextureDesc{
        .width = static_cast<std::uint32_t>(atlas_width),
        .height = static_cast<std::uint32_t>(atlas_height),
        .format = rhi::TextureFormat::Rgba8Unorm,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination,
        .debugName = "ImGui Font Atlas",
    });
    if (!font_texture) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to create the ImGui font texture"});
    }
    implementation->font_texture = *font_texture;

    const std::uint64_t atlas_bytes =
        static_cast<std::uint64_t>(atlas_width) * static_cast<std::uint64_t>(atlas_height) * 4U;
    auto upload = options.device->createBuffer(rhi::BufferDesc{
        .size = atlas_bytes,
        .usage = rhi::BufferUsage::CopySource,
        .memory = rhi::MemoryType::Upload,
        .debugName = "ImGui Font Upload",
    });
    if (!upload) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to create the font upload buffer"});
    }
    const auto atlas_span = std::span<const std::byte>(reinterpret_cast<const std::byte *>(pixels), atlas_bytes);
    if (!options.device->writeBuffer(*upload, atlas_span)) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to upload the ImGui font atlas"});
    }
    auto commands = options.device->beginCommandList(rhi::QueueType::Graphics);
    if (!commands) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to record the font upload"});
    }
    (*commands)->copyBufferToTexture(*upload, *font_texture);
    const std::array lists{*commands};
    if (!options.device->submit(rhi::QueueType::Graphics, lists, {})) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to submit the font upload"});
    }
    static_cast<void>(options.device->destroy(*upload));

    auto font_view = options.device->createTextureView({.texture = *font_texture, .debugName = "ImGui Font View"});
    if (!font_view) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to create the font texture view"});
    }
    implementation->font_view = *font_view;

    const std::array bind_entries{
        rhi::BindGroupEntry{0, {}, {}, *uniform_buffer, 0, sizeof(UiUniforms)},
        rhi::BindGroupEntry{1, *font_view, *sampler, {}, 0, 0},
    };
    auto bind_group = options.device->createBindGroup(
        {.layout = *bind_group_layout, .entries = bind_entries, .debugName = "ImGui Bind Group"});
    if (!bind_group) {
        ImGui::DestroyContext();
        return std::unexpected(UiError{"Failed to create the ImGui bind group"});
    }
    implementation->bind_group = *bind_group;
    io.Fonts->SetTexID((ImTextureID)(std::uintptr_t)1);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    if (options.native_window) {
        implementation->sdl_backend = ImGui_ImplSDL3_InitForVulkan(static_cast<SDL_Window *>(options.native_window));
        if (implementation->sdl_backend && options.enable_platform_viewports &&
            (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0) {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigViewportsNoAutoMerge = true;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
            io.BackendRendererUserData = implementation.get();
            auto &platform = ImGui::GetPlatformIO();
            platform.Renderer_CreateWindow = &Impl::createPlatformWindow;
            platform.Renderer_DestroyWindow = &Impl::destroyPlatformWindow;
            platform.Renderer_RenderWindow = &Impl::renderPlatformWindow;
        }
    }

    log::info(k_log_category, "ImGui {} ready (atlas {}x{})", IMGUI_VERSION, atlas_width, atlas_height);
    return std::unique_ptr<ImGuiLayer>(new ImGuiLayer(std::move(implementation)));
}

ImGuiLayer::ImGuiLayer(std::unique_ptr<Impl> implementation) noexcept : m_impl(std::move(implementation)) {}

ImGuiLayer::~ImGuiLayer() {
    if (!m_impl) {
        return;
    }
    if (ImGui::GetIO().IniFilename) {
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        ImGui::GetIO().IniFilename = nullptr;
    }
    if (m_impl->sdl_backend) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::GetIO().BackendRendererUserData = nullptr;
    ImGui::DestroyContext();
    rhi::Device *device = m_impl->device;
    if (device == nullptr) {
        return;
    }
    static_cast<void>(device->waitIdle());
    for (const auto &[id, group] : m_impl->images) {
        static_cast<void>(device->destroy(group));
    }
    static_cast<void>(device->destroy(m_impl->bind_group));
    static_cast<void>(device->destroy(m_impl->font_view));
    static_cast<void>(device->destroy(m_impl->font_texture));
    static_cast<void>(device->destroy(m_impl->uniform_buffer));
    static_cast<void>(device->destroy(m_impl->vertex_buffer));
    static_cast<void>(device->destroy(m_impl->index_buffer));
    static_cast<void>(device->destroy(m_impl->pipeline));
    static_cast<void>(device->destroy(m_impl->pipeline_layout));
    static_cast<void>(device->destroy(m_impl->bind_group_layout));
    static_cast<void>(device->destroy(m_impl->sampler));
    static_cast<void>(device->destroy(m_impl->vertex_shader));
    static_cast<void>(device->destroy(m_impl->fragment_shader));
}

void ImGuiLayer::processEvent(const WindowEvent &event) {
    if (m_impl->sdl_backend) {
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    switch (event.type) {
    case WindowEventType::MouseMotion:
        io.AddMousePosEvent(event.x, event.y);
        break;
    case WindowEventType::MouseButtonDown:
    case WindowEventType::MouseButtonUp: {
        const int button = resolve_mouse_button(event.code);
        if (button >= 0) {
            io.AddMousePosEvent(event.x, event.y);
            io.AddMouseButtonEvent(button, event.type == WindowEventType::MouseButtonDown);
        }
        break;
    }
    case WindowEventType::MouseWheel:
        io.AddMouseWheelEvent(event.x, event.y);
        break;
    case WindowEventType::KeyDown:
    case WindowEventType::KeyUp: {
        const ImGuiKey key = resolve_key(event.code);
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, event.type == WindowEventType::KeyDown);
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.modifiers & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.modifiers & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.modifiers & SDL_KMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (event.modifiers & SDL_KMOD_GUI) != 0);
        break;
    }
    case WindowEventType::TextInput:
        if (event.code != 0) {
            io.AddInputCharacter(event.code);
        }
        break;
    case WindowEventType::CloseRequested:
    case WindowEventType::Resized:
        break;
    }
}

void ImGuiLayer::processNativeEvent(const void *event) {
    if (m_impl->sdl_backend && event) {
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event *>(event));
    }
}

void ImGuiLayer::beginFrame(std::uint32_t width, std::uint32_t height, float delta_seconds) {
    ImGuiIO &io = ImGui::GetIO();
    if (m_impl->sdl_backend) {
        ImGui_ImplSDL3_NewFrame();
    } else {
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    }
    io.DeltaTime = std::max(delta_seconds, 1.0F / 1000.0F);
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() { ImGui::Render(); }

bool ImGuiLayer::usesPlatformViewports() const noexcept {
    return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
}

void ImGuiLayer::renderPlatformWindows() {
    if (usesPlatformViewports()) {
        const auto primary_lists = m_impl->draw_list_count;
        const auto primary_calls = m_impl->draw_call_count;
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        m_impl->draw_list_count = primary_lists;
        m_impl->draw_call_count = primary_calls;
    }
}

std::expected<std::uint64_t, UiError> ImGuiLayer::registerTexture(rhi::TextureViewHandle view) {
    const std::array entries{
        rhi::BindGroupEntry{0, {}, {}, m_impl->uniform_buffer, 0, sizeof(UiUniforms)},
        rhi::BindGroupEntry{1, view, m_impl->sampler, {}, 0, 0},
    };
    const auto group = m_impl->device->createBindGroup(
        {.layout = m_impl->bind_group_layout, .entries = entries, .debugName = "ImGui Image"});
    if (!group) {
        return std::unexpected(UiError{"Failed to bind the ImGui image"});
    }
    const auto id = m_impl->next_image_id++;
    m_impl->images.emplace(id, *group);
    return id;
}

void ImGuiLayer::unregisterTexture(std::uint64_t id) {
    const auto found = m_impl->images.find(id);
    if (found != m_impl->images.end()) {
        static_cast<void>(m_impl->device->destroy(found->second));
        m_impl->images.erase(found);
    }
}

void ImGuiLayer::render(rhi::CommandList &commands, rhi::TextureHandle target, bool present_after,
                        const std::array<float, 4> &clear_color) {
    m_impl->recordDrawData(commands, ImGui::GetDrawData(), target, present_after, clear_color);
}

void ImGuiLayer::Impl::recordDrawData(rhi::CommandList &commands, ImDrawData *draw_data, rhi::TextureHandle target,
                                      bool present_after, const std::array<float, 4> &clear_color) {
    draw_list_count = 0;
    draw_call_count = 0;

    commands.beginRenderPass(rhi::RenderPassBeginInfo{
        .color_target = target,
        .present_after = present_after,
        .clear_color = clear_color,
        .clear = true,
        .debugName = "Editor UI",
    });

    if (draw_data != nullptr && draw_data->TotalVtxCount > 0) {
        const std::uint64_t vertex_bytes = static_cast<std::uint64_t>(draw_data->TotalVtxCount) * sizeof(UiVertex);
        const std::uint64_t index_bytes = static_cast<std::uint64_t>(draw_data->TotalIdxCount) * sizeof(std::uint32_t);
        const bool buffers_ready =
            ensure_buffer(vertex_buffer, vertex_capacity, vertex_bytes, rhi::BufferUsage::Vertex, "ImGui Vertices") &&
            ensure_buffer(index_buffer, index_capacity, index_bytes, rhi::BufferUsage::Index, "ImGui Indices");

        std::uint64_t vertex_offset = 0;
        std::uint64_t index_offset = 0;
        bool uploaded = buffers_ready;
        for (int list_index = 0; uploaded && list_index < draw_data->CmdListsCount; ++list_index) {
            const ImDrawList *list = draw_data->CmdLists[list_index];
            const auto vertices =
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(list->VtxBuffer.Data),
                                           static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
            const auto indices =
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(list->IdxBuffer.Data),
                                           static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            uploaded = device->writeBuffer(vertex_buffer, vertices, vertex_offset) &&
                       device->writeBuffer(index_buffer, indices, index_offset);
            vertex_offset += vertices.size();
            index_offset += indices.size();
        }

        if (uploaded) {
            UiUniforms uniforms{};
            const ImVec2 position = draw_data->DisplayPos;
            const ImVec2 size = draw_data->DisplaySize;
            uniforms.scale[0] = 2.0F / size.x;
            uniforms.scale[1] = 2.0F / size.y;
            uniforms.translate[0] = -1.0F - position.x * uniforms.scale[0];
            uniforms.translate[1] = -1.0F - position.y * uniforms.scale[1];
            const auto uniform_bytes =
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(&uniforms), sizeof(uniforms));
            uploaded = static_cast<bool>(device->writeBuffer(uniform_buffer, uniform_bytes));
        }

        if (uploaded) {
            commands.bindVertexBuffer(vertex_buffer);
            commands.bindIndexBuffer(index_buffer, 0, sizeof(ImDrawIdx) == 4);

            rhi::BindGroupHandle bound_group;
            const ImVec2 position = draw_data->DisplayPos;
            const ImVec2 framebuffer = draw_data->FramebufferScale;
            std::uint32_t global_vertex_offset = 0;
            std::uint32_t global_index_offset = 0;
            draw_list_count = static_cast<std::uint32_t>(draw_data->CmdListsCount);
            for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
                const ImDrawList *list = draw_data->CmdLists[list_index];
                for (const ImDrawCmd &command : list->CmdBuffer) {
                    if (command.UserCallback != nullptr) {
                        continue;
                    }
                    const float clip_left = (command.ClipRect.x - position.x) * framebuffer.x;
                    const float clip_top = (command.ClipRect.y - position.y) * framebuffer.y;
                    const float clip_right = (command.ClipRect.z - position.x) * framebuffer.x;
                    const float clip_bottom = (command.ClipRect.w - position.y) * framebuffer.y;
                    if (clip_right <= clip_left || clip_bottom <= clip_top) {
                        continue;
                    }
                    const auto x = static_cast<std::int32_t>(std::max(clip_left, 0.0F));
                    const auto y = static_cast<std::int32_t>(std::max(clip_top, 0.0F));
                    const auto right =
                        static_cast<std::int32_t>(std::min(clip_right, draw_data->DisplaySize.x * framebuffer.x));
                    const auto bottom =
                        static_cast<std::int32_t>(std::min(clip_bottom, draw_data->DisplaySize.y * framebuffer.y));
                    if (right <= x || bottom <= y) {
                        continue;
                    }
                    rhi::BindGroupHandle group = bind_group;
                    if (command.GetTexID() != ImTextureID{1}) {
                        const auto texture = images.find(command.GetTexID());
                        if (texture == images.end()) {
                            continue;
                        }
                        group = texture->second;
                    }
                    if (bound_group != group) {
                        const std::array groups{group};
                        commands.bindGraphicsPipeline(pipeline, groups);
                        bound_group = group;
                    }
                    commands.setScissor(x, y, static_cast<std::uint32_t>(right - x),
                                        static_cast<std::uint32_t>(bottom - y));
                    commands.drawIndexed(command.ElemCount, 1, global_index_offset + command.IdxOffset,
                                         static_cast<std::int32_t>(global_vertex_offset + command.VtxOffset));
                    ++draw_call_count;
                }
                global_vertex_offset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
                global_index_offset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
            }
        }
    }

    commands.endRenderPass();
}

void ImGuiLayer::Impl::createPlatformWindow(ImGuiViewport *viewport) {
    viewport->RendererUserData = new PlatformSwapchain{};
}

void ImGuiLayer::Impl::destroyPlatformWindow(ImGuiViewport *viewport) {
    auto state = std::unique_ptr<PlatformSwapchain>(static_cast<PlatformSwapchain *>(viewport->RendererUserData));
    viewport->RendererUserData = nullptr;
    if (state && state->handle.isValid()) {
        auto *self = static_cast<Impl *>(ImGui::GetIO().BackendRendererUserData);
        static_cast<void>(self->device->destroy(state->handle));
    }
}

void ImGuiLayer::Impl::renderPlatformWindow(ImGuiViewport *viewport, void *) {
    auto *self = static_cast<Impl *>(ImGui::GetIO().BackendRendererUserData);
    auto *data = static_cast<PlatformSwapchain *>(viewport->RendererUserData);
    auto *window =
        SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle)));
    if (!data || !window || (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0) {
        return;
    }
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height) || width <= 0 || height <= 0) {
        return;
    }
    const auto pixel_width = static_cast<std::uint32_t>(width);
    const auto pixel_height = static_cast<std::uint32_t>(height);
    auto &device = *self->device;
    if (!data->handle.isValid()) {
        const auto created = device.createSwapchain({.width = pixel_width,
                                                     .height = pixel_height,
                                                     .vsync = false,
                                                     .debugName = "Editor Platform Viewport",
                                                     .native_window = window});
        if (!created) {
            log::warning(k_log_category, "Could not open a platform viewport ({})", static_cast<int>(created.error()));
            viewport->PlatformRequestClose = true;
            return;
        }
        data->handle = *created;
        const auto info = device.getSwapchainInfo(*created);
        if (!info || info->format != self->color_format) {
            log::warning(k_log_category, "Platform viewport does not support the editor swapchain format");
            viewport->PlatformRequestClose = true;
            return;
        }
        data->width = pixel_width;
        data->height = pixel_height;
        log::info(k_log_category, "Platform viewport ready ({} x {})", pixel_width, pixel_height);
    } else if (data->width != pixel_width || data->height != pixel_height) {
        if (!device.resizeSwapchain(data->handle, pixel_width, pixel_height)) {
            log::warning(k_log_category, "Could not resize a platform viewport");
            viewport->PlatformRequestClose = true;
            return;
        }
        data->width = pixel_width;
        data->height = pixel_height;
        log::debug(k_log_category, "Platform viewport resized ({} x {})", pixel_width, pixel_height);
    }
    const auto target = device.acquireNextImage(data->handle);
    if (!target) {
        if (target.error() == rhi::DeviceError::Outdated) {
            data->width = 0;
        } else {
            viewport->PlatformRequestClose = true;
        }
        return;
    }
    const auto commands = device.beginCommandList(rhi::QueueType::Graphics);
    if (!commands) {
        viewport->PlatformRequestClose = true;
        return;
    }
    if (viewport->DrawData && viewport->DrawData->DisplaySize.x > 0 && viewport->DrawData->DisplaySize.y > 0) {
        viewport->DrawData->FramebufferScale = {
            static_cast<float>(width) / viewport->DrawData->DisplaySize.x,
            static_cast<float>(height) / viewport->DrawData->DisplaySize.y,
        };
    }
    self->recordDrawData(**commands, viewport->DrawData, *target, true, {0.10F, 0.11F, 0.13F, 1.0F});
    const std::array lists{*commands};
    if (!device.submit(rhi::QueueType::Graphics, lists, {})) {
        viewport->PlatformRequestClose = true;
        return;
    }
    if (const auto presented = device.present(data->handle); !presented) {
        if (presented.error() == rhi::DeviceError::Outdated) {
            data->width = 0;
        } else {
            viewport->PlatformRequestClose = true;
        }
    }
}

std::uint32_t ImGuiLayer::getDrawListCount() const noexcept { return m_impl ? m_impl->draw_list_count : 0; }

std::uint32_t ImGuiLayer::getDrawCallCount() const noexcept { return m_impl ? m_impl->draw_call_count : 0; }

} // namespace star::editor
