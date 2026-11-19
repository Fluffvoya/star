#include "Core/Core.hpp"
#include "Platform/Platform.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

namespace {

constexpr star::LogLevel kCategoryLevel = star::LogLevel::Debug;
const star::LogCategory kLogCategory("HelloWindow", kCategoryLevel);

[[nodiscard]] std::uint64_t ParseFrameLimit(int argc, char **argv) {
    constexpr std::string_view prefix = "--frames=";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (!argument.starts_with(prefix)) {
            continue;
        }

        std::uint64_t frameLimit = 0;
        const std::string_view value = argument.substr(prefix.size());
        const auto parseResult = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
        if (parseResult.ec == std::errc{} && parseResult.ptr == value.data() + value.size()) {
            return frameLimit;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const std::uint64_t frameLimit = ParseFrameLimit(argc, argv);

    auto platform = star::PlatformContext::Initialize();
    if (!platform) {
        star::log::Error(kLogCategory, "Platform initialization failed: {}", platform.error().message);
        return 1;
    }

    star::WindowDescriptor descriptor;
    descriptor.title = "Star Engine M0";
    auto window = star::CreateWindow(descriptor);
    if (!window) {
        star::log::Error(kLogCategory, "Window creation failed: {}", window.error().message);
        return 1;
    }

    star::FrameClock frameClock;
    std::uint64_t frameIndex = 0;
    while (!(*window)->ShouldClose()) {
        static_cast<void>((*window)->PollEvents());

        const double deltaSeconds = frameClock.Tick();
        const double milliseconds = deltaSeconds * 1000.0;
        star::log::Info(kLogCategory, "frame={} size={}x{} time={:.3f} ms", frameIndex, (*window)->GetWidth(),
                        (*window)->GetHeight(), milliseconds);

        ++frameIndex;
        if (frameLimit != 0 && frameIndex >= frameLimit) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    return 0;
}
