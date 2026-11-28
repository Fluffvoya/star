#include "core/core.hpp"
#include "editor-ui/editor_shell.hpp"
#include "editor-ui/imgui_layer.hpp"
#include "platform/platform.hpp"
#include "rhi/rhi.hpp"
#include "rhi/vulkan.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

const star::LogCategory k_log_category("StarEditor", star::LogLevel::Debug);

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

[[nodiscard]] std::filesystem::path parse_path_option(int argc, char **argv, std::string_view prefix) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument.starts_with(prefix)) {
            return std::filesystem::path(argument.substr(prefix.size()));
        }
    }
    return {};
}

[[nodiscard]] bool write_ppm(const std::filesystem::path &path, std::uint32_t width, std::uint32_t height,
                             std::span<const std::byte> pixels, bool bgra) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << "P6\n" << width << " " << height << "\n255\n";
    std::vector<char> row(static_cast<std::size_t>(width) * 3U);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t source = (y * width + x) * 4U;
            const std::size_t target = x * 3U;
            row[target + 0] = std::to_integer<char>(pixels[source + (bgra ? 2U : 0U)]);
            row[target + 1] = std::to_integer<char>(pixels[source + 1U]);
            row[target + 2] = std::to_integer<char>(pixels[source + (bgra ? 0U : 2U)]);
        }
        file.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    return file.good();
}

[[nodiscard]] bool prepare_saved_directories() {
    std::error_code error;
    std::filesystem::create_directories("Saved/Logs", error);
    if (error) {
        return false;
    }
    return static_cast<bool>(star::log::setFileSink("Saved/Logs/StarEditor.log"));
}

} // namespace

int main(int argc, char **argv) {
    const std::uint64_t frame_limit = parse_frame_limit(argc, argv);
    const bool validation = has_flag(argc, argv, "--validation");
    if (!prepare_saved_directories()) {
        star::log::warning(k_log_category, "Log directory unavailable; continuing without a file sink");
    }

    auto platform = star::PlatformContext::initialize();
    if (!platform) {
        star::log::error(k_log_category, "Platform initialization failed: {}", platform.error().message);
        return 1;
    }
    auto window = star::createWindow(star::WindowDescriptor{
        .title = "Star Editor",
        .width = 1600,
        .height = 900,
    });
    if (!window) {
        star::log::error(k_log_category, "Window creation failed: {}", window.error().message);
        return 1;
    }

    star::rhi::DeviceCreateInfo create_info{};
    create_info.enableValidation = validation;
    create_info.native_window = (*window)->getNativeHandle();
    auto device_result = star::rhi::createVulkanDevice(create_info);
    if (!device_result) {
        star::log::error(k_log_category, "Vulkan device unavailable ({})", static_cast<int>(device_result.error()));
        return 1;
    }
    std::unique_ptr<star::rhi::Device> device = std::move(*device_result);

    const auto width = static_cast<std::uint32_t>((*window)->getWidth() > 0 ? (*window)->getWidth() : 1600);
    const auto height = static_cast<std::uint32_t>((*window)->getHeight() > 0 ? (*window)->getHeight() : 900);
    auto swapchain_result = device->createSwapchain(star::rhi::SwapchainDesc{
        .width = width,
        .height = height,
        .vsync = true,
        .debugName = "Editor Swapchain",
    });
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
    star::log::info(k_log_category, "Editing on {}", device->getDeviceName());

    const std::filesystem::path executable_dir = std::filesystem::path(argv[0]).parent_path();
    auto layer_result = star::editor::ImGuiLayer::create(star::editor::ImGuiLayerOptions{
        .device = device.get(),
        .color_format = swapchain_info->format,
        .shader_directory = executable_dir,
        .enable_docking = true,
        .font_size = 16.0F,
    });
    if (!layer_result) {
        star::log::error(k_log_category, "ImGui initialization failed: {}", layer_result.error().message);
        return 1;
    }
    std::unique_ptr<star::editor::ImGuiLayer> layer = std::move(*layer_result);
    star::editor::EditorShell shell;

    const auto recreate_swapchain = [&]() {
        const auto next_width = static_cast<std::uint32_t>((*window)->getWidth() > 0 ? (*window)->getWidth() : 1600);
        const auto next_height = static_cast<std::uint32_t>((*window)->getHeight() > 0 ? (*window)->getHeight() : 900);
        const auto resized = device->resizeSwapchain(swapchain, next_width, next_height);
        if (!resized) {
            star::log::error(k_log_category, "Swapchain resize failed ({})", static_cast<int>(resized.error()));
        }
        return static_cast<bool>(resized);
    };

    const std::array<float, 4> clear_color{0.10F, 0.11F, 0.13F, 1.0F};
    const std::filesystem::path screenshot_path = parse_path_option(argc, argv, "--screenshot=");
    bool capture_pending = !screenshot_path.empty();

    auto previous = std::chrono::steady_clock::now();
    std::uint64_t rendered = 0;
    bool running = true;
    while (running && (frame_limit == 0 || rendered < frame_limit)) {
        const auto frame_start = std::chrono::steady_clock::now();
        const float delta_seconds = std::chrono::duration<float>(frame_start - previous).count();
        previous = frame_start;

        for (const star::WindowEvent &event : (*window)->pollEvents()) {
            layer->processEvent(event);
            if (event.type == star::WindowEventType::CloseRequested) {
                running = false;
            }
        }
        if (!running || (*window)->shouldClose() || shell.shouldQuit()) {
            break;
        }

        const auto drawable_width = static_cast<std::uint32_t>(std::max((*window)->getWidth(), 0));
        const auto drawable_height = static_cast<std::uint32_t>(std::max((*window)->getHeight(), 0));
        if (drawable_width == 0 || drawable_height == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            previous = std::chrono::steady_clock::now();
            continue;
        }

        device->beginFrame();
        auto image_result = device->acquireNextImage(swapchain);
        if (!image_result) {
            if (image_result.error() == star::rhi::DeviceError::Outdated) {
                recreate_swapchain();
                device->endFrame();
                continue;
            }
            star::log::error(k_log_category, "Image acquire failed ({})", static_cast<int>(image_result.error()));
            running = false;
            device->endFrame();
            break;
        }

        layer->beginFrame(drawable_width, drawable_height, delta_seconds);
        const float frame_ms = delta_seconds * 1000.0F;
        star::editor::EditorShellStatus status{};
        status.frame_time_ms = frame_ms;
        status.fps = frame_ms > 0.0F ? 1000.0F / frame_ms : 0.0F;
        status.width = drawable_width;
        status.height = drawable_height;
        status.draw_calls = layer->getDrawCallCount();
        status.device_name = device->getDeviceName();
        shell.draw(status);
        layer->endFrame();

        auto commands_result = device->beginCommandList(star::rhi::QueueType::Graphics);
        if (!commands_result) {
            star::log::error(k_log_category, "Command list creation failed");
            running = false;
            device->endFrame();
            break;
        }
        star::rhi::CommandList *commands = *commands_result;
        commands->beginDebugMarker("Editor Frame");

        star::rhi::TextureHandle capture_texture;
        star::rhi::BufferHandle capture_readback;
        const std::uint64_t capture_bytes = static_cast<std::uint64_t>(drawable_width) * drawable_height * 4U;
        if (capture_pending && rendered >= 2) {
            auto texture = device->createTexture(star::rhi::TextureDesc{
                .width = drawable_width,
                .height = drawable_height,
                .format = swapchain_info->format,
                .usage = star::rhi::TextureUsage::RenderTarget | star::rhi::TextureUsage::Sampled |
                         star::rhi::TextureUsage::CopySource,
                .debugName = "Editor Capture",
            });
            auto readback = device->createBuffer(star::rhi::BufferDesc{
                .size = capture_bytes,
                .usage = star::rhi::BufferUsage::CopyDestination,
                .memory = star::rhi::MemoryType::Readback,
                .debugName = "Editor Capture Readback",
            });
            if (texture && readback) {
                capture_texture = *texture;
                capture_readback = *readback;
                layer->render(*commands, capture_texture, false, clear_color);
                commands->copyTextureToBuffer(capture_texture, capture_readback);
            }
        }

        layer->render(*commands, *image_result, true, clear_color);
        commands->endDebugMarker();

        const std::array lists{commands};
        if (!device->submit(star::rhi::QueueType::Graphics, lists, {})) {
            star::log::error(k_log_category, "Frame submission failed");
            running = false;
            device->endFrame();
            break;
        }
        if (auto presented = device->present(swapchain); !presented) {
            if (presented.error() == star::rhi::DeviceError::Outdated) {
                recreate_swapchain();
            } else {
                star::log::error(k_log_category, "Present failed ({})", static_cast<int>(presented.error()));
                running = false;
            }
        }
        device->endFrame();
        ++rendered;

        if (capture_texture.isValid()) {
            std::vector<std::byte> pixels(static_cast<std::size_t>(capture_bytes));
            const bool read = static_cast<bool>(device->readBuffer(capture_readback, pixels));
            const bool written = read && write_ppm(screenshot_path, drawable_width, drawable_height, pixels,
                                                   swapchain_info->format == star::rhi::TextureFormat::Bgra8Unorm);
            star::log::info(k_log_category, "Screenshot {}: {}", written ? "written" : "failed",
                            screenshot_path.string());
            static_cast<void>(device->destroy(capture_texture));
            static_cast<void>(device->destroy(capture_readback));
            capture_pending = false;
        }
    }

    shell.requestQuit();
    star::log::info(k_log_category, "Last frame recorded {} ImGui draw calls", layer->getDrawCallCount());
    layer.reset();
    static_cast<void>(device->waitIdle());
    static_cast<void>(device->destroy(swapchain));
    star::log::info(k_log_category, "Editor stopped after {} frames", rendered);
    return running ? 0 : 1;
}
