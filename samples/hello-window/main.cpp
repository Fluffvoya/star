#include "core/core.hpp"
#include "platform/platform.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

namespace {

constexpr star::LogLevel k_category_level = star::LogLevel::Debug;
const star::LogCategory k_log_category("HelloWindow", k_category_level);

[[nodiscard]] std::uint64_t parseFrameLimit(int argc, char **argv) {
    constexpr std::string_view prefix = "--frames=";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (!argument.starts_with(prefix)) {
            continue;
        }

        std::uint64_t frame_limit = 0;
        const std::string_view value = argument.substr(prefix.size());
        const auto parse_result = std::from_chars(value.data(), value.data() + value.size(), frame_limit);
        if (parse_result.ec == std::errc{} && parse_result.ptr == value.data() + value.size()) {
            return frame_limit;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const std::uint64_t frame_limit = parseFrameLimit(argc, argv);

    auto platform = star::PlatformContext::initialize();
    if (!platform) {
        star::log::error(k_log_category, "Platform initialization failed: {}", platform.error().message);
        return 1;
    }

    star::WindowDescriptor descriptor;
    descriptor.title = "Star Engine M0";
    auto window = star::createWindow(descriptor);
    if (!window) {
        star::log::error(k_log_category, "Window creation failed: {}", window.error().message);
        return 1;
    }

    star::FrameClock frame_clock;
    std::uint64_t frame_index = 0;
    for (;;) {
        static_cast<void>((*window)->pollEvents());
        if ((*window)->shouldClose()) {
            break;
        }

        // Wayland maps the window after the first buffer is presented.
        if (const auto result = (*window)->presentSolidColor(32, 36, 44); !result) {
            star::log::error(k_log_category, "Frame presentation failed: {}", result.error().message);
            return 1;
        }

        const double delta_seconds = frame_clock.tick();
        const double milliseconds = delta_seconds * 1000.0;
        star::log::info(k_log_category, "frame={} size={}x{} time={:.3f} ms", frame_index, (*window)->getWidth(),
                        (*window)->getHeight(), milliseconds);

        ++frame_index;
        if (frame_limit != 0 && frame_index >= frame_limit) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
