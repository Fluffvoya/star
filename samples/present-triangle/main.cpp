#include "core/core.hpp"
#include "platform/platform.hpp"
#include "rhi/rhi.hpp"
#include "rhi/vulkan.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

const star::LogCategory k_log_category("PresentTriangle", star::LogLevel::Info);

[[nodiscard]] bool has_flag(int argc, char **argv, std::string_view flag) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == flag) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::uint64_t parse_frame_limit(int argc, char **argv) {
    constexpr std::string_view prefix = "--frames=";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (!argument.starts_with(prefix)) {
            continue;
        }
        std::uint64_t frames = 0;
        const std::string_view value = argument.substr(prefix.size());
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frames);
        if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) {
            return frames;
        }
    }
    return 0;
}

[[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        return {};
    }
    std::vector<std::uint32_t> spirv(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(spirv.data(), bytes.data(), bytes.size());
    return spirv;
}

} // namespace

int main(int argc, char **argv) {
    const std::uint64_t frame_limit = parse_frame_limit(argc, argv);
    const std::filesystem::path executable_dir = std::filesystem::path(argv[0]).parent_path();

    auto platform = star::PlatformContext::initialize();
    if (!platform) {
        star::log::warning(k_log_category, "Platform unavailable: {}", platform.error().message);
        return 77;
    }
    auto window = star::createWindow(star::WindowDescriptor{
        .title = "Star Present Triangle",
        .width = 1280,
        .height = 720,
    });
    if (!window) {
        star::log::warning(k_log_category, "Window creation failed: {}", window.error().message);
        return 77;
    }

    star::rhi::DeviceCreateInfo create_info{};
    create_info.enableValidation = has_flag(argc, argv, "--validation");
    create_info.native_window = (*window)->getNativeHandle();
    auto device_result = star::rhi::createVulkanDevice(create_info);
    if (!device_result) {
        star::log::warning(k_log_category, "Vulkan device unavailable ({})", static_cast<int>(device_result.error()));
        return device_result.error() == star::rhi::DeviceError::BackendUnavailable ? 77 : 1;
    }
    std::unique_ptr<star::rhi::Device> device = std::move(*device_result);
    star::log::info(k_log_category, "Presentation device ready: {}", device->getCapabilities().supportsPresentation);

    auto swapchain_result = device->createSwapchain(star::rhi::SwapchainDesc{
        .width = 1280, .height = 720, .vsync = true, .debugName = "Present Triangle Swapchain"});
    if (!swapchain_result) {
        star::log::error(k_log_category, "Swapchain creation failed ({})", static_cast<int>(swapchain_result.error()));
        return 1;
    }
    const star::rhi::SwapchainHandle swapchain = *swapchain_result;
    auto swapchain_info = device->getSwapchainInfo(swapchain);
    if (!swapchain_info) {
        star::log::error(k_log_category, "Swapchain info unavailable");
        return 1;
    }

    const std::vector<std::uint32_t> vertex_spirv = load_spirv(executable_dir / "triangle-vertex.spv");
    const std::vector<std::uint32_t> fragment_spirv = load_spirv(executable_dir / "triangle-fragment.spv");
    if (vertex_spirv.empty() || fragment_spirv.empty()) {
        star::log::error(k_log_category, "Missing compiled shaders next to the executable");
        return 1;
    }
    auto vertex_shader = device->createShaderModule(star::rhi::ShaderModuleDesc{
        .stage = star::rhi::ShaderStage::Vertex,
        .spirv = vertex_spirv,
        .debugName = "Triangle Vertex",
    });
    auto fragment_shader = device->createShaderModule(star::rhi::ShaderModuleDesc{
        .stage = star::rhi::ShaderStage::Fragment,
        .spirv = fragment_spirv,
        .debugName = "Triangle Fragment",
    });
    if (!vertex_shader || !fragment_shader) {
        star::log::error(k_log_category, "Shader module creation failed");
        return 1;
    }
    auto layout_result = device->createPipelineLayout(star::rhi::PipelineLayoutDesc{
        .bind_groups = {},
        .push_constant_size = 0,
        .debugName = "Triangle Layout",
    });
    if (!layout_result) {
        star::log::error(k_log_category, "Pipeline layout creation failed");
        return 1;
    }
    const std::array<star::rhi::TextureFormat, 1> color_formats{swapchain_info->format};
    auto pipeline_result = device->createGraphicsPipeline(star::rhi::GraphicsPipelineDesc{
        .layout = *layout_result,
        .vertex_shader = *vertex_shader,
        .fragment_shader = *fragment_shader,
        .vertex_buffers = {},
        .color_formats = color_formats,
        .debugName = "Triangle Pipeline",
    });
    if (!pipeline_result) {
        star::log::error(k_log_category, "Graphics pipeline creation failed ({})",
                         static_cast<int>(pipeline_result.error()));
        return 1;
    }

    const auto recreate_swapchain = [&]() {
        const std::int32_t width = (*window)->getWidth();
        const std::int32_t height = (*window)->getHeight();
        const auto resized = device->resizeSwapchain(swapchain, static_cast<std::uint32_t>(width > 0 ? width : 1280),
                                                     static_cast<std::uint32_t>(height > 0 ? height : 720));
        if (!resized) {
            star::log::error(k_log_category, "Swapchain resize failed ({})", static_cast<int>(resized.error()));
        }
        return static_cast<bool>(resized);
    };

    std::uint64_t rendered = 0;
    bool running = true;
    while (running && (frame_limit == 0 || rendered < frame_limit)) {
        const std::span<const star::WindowEvent> events = (*window)->pollEvents();
        (void)events;
        if ((*window)->shouldClose()) {
            break;
        }

        device->beginFrame();
        auto image_result = device->acquireNextImage(swapchain);
        if (!image_result) {
            if (image_result.error() == star::rhi::DeviceError::Outdated) {
                if (!recreate_swapchain()) {
                    break;
                }
                device->endFrame();
                continue;
            }
            star::log::error(k_log_category, "Image acquire failed ({})", static_cast<int>(image_result.error()));
            running = false;
            device->endFrame();
            break;
        }

        auto commands_result = device->beginCommandList(star::rhi::QueueType::Graphics);
        if (!commands_result) {
            star::log::error(k_log_category, "Command list creation failed");
            running = false;
            device->endFrame();
            break;
        }
        star::rhi::CommandList *commands = *commands_result;
        commands->beginDebugMarker("Present Triangle");
        commands->beginRenderPass(star::rhi::RenderPassBeginInfo{
            .color_target = *image_result,
            .present_after = true,
            .clear_color = {0.05F, 0.06F, 0.08F, 1.0F},
            .clear = true,
            .debugName = "Triangle Pass",
        });
        commands->bindGraphicsPipeline(*pipeline_result, {});
        commands->draw(3);
        commands->endRenderPass();
        commands->endDebugMarker();

        const std::array<star::rhi::CommandList *, 1> lists{commands};
        auto submitted = device->submit(star::rhi::QueueType::Graphics, lists, {});
        if (!submitted) {
            star::log::error(k_log_category, "Submit failed ({})", static_cast<int>(submitted.error()));
            running = false;
            device->endFrame();
            break;
        }
        auto presented = device->present(swapchain);
        if (!presented && presented.error() == star::rhi::DeviceError::Outdated) {
            recreate_swapchain();
        } else if (!presented) {
            star::log::error(k_log_category, "Present failed ({})", static_cast<int>(presented.error()));
            running = false;
        }
        device->endFrame();
        ++rendered;
    }

    if (const auto idle = device->waitIdle(); !idle) {
        star::log::warning(k_log_category, "Device wait failed before shutdown");
    }
    static_cast<void>(device->destroy(*pipeline_result));
    static_cast<void>(device->destroy(*layout_result));
    static_cast<void>(device->destroy(*vertex_shader));
    static_cast<void>(device->destroy(*fragment_shader));
    static_cast<void>(device->destroy(swapchain));
    star::log::info(k_log_category, "Rendered {} frames", rendered);
    return running ? 0 : 1;
}
