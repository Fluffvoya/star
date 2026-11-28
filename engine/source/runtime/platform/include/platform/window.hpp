#pragma once

#include "platform/export.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace star {

struct PlatformError {
    std::string message;
};

enum class WindowEventType : std::uint8_t {
    CloseRequested,
    Resized,
    KeyDown,
    KeyUp,
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
};

struct WindowEvent {
    WindowEventType type = WindowEventType::CloseRequested;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t code = 0;
    std::uint32_t modifiers = 0;
    float x = 0.0F;
    float y = 0.0F;
    float xrel = 0.0F;
    float yrel = 0.0F;
};

struct WindowDescriptor {
    std::string title = "Star Engine";
    std::int32_t width = 1280;
    std::int32_t height = 720;
};

class STAR_PLATFORM_API Window final {
  public:
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    Window(Window &&) noexcept;
    Window &operator=(Window &&) noexcept;

    [[nodiscard]] std::span<const WindowEvent> pollEvents();
    // Present an RGB frame using the window surface on the main thread.
    [[nodiscard]] std::expected<void, PlatformError> presentSolidColor(std::uint8_t red, std::uint8_t green,
                                                                       std::uint8_t blue);
    // SDL_Window* backing this window. Valid until the window is destroyed.
    [[nodiscard]] void *getNativeHandle() const noexcept;
    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] std::int32_t getWidth() const;
    [[nodiscard]] std::int32_t getHeight() const;
    [[nodiscard]] std::string_view getTitle() const;

  private:
    friend std::expected<std::unique_ptr<Window>, PlatformError> createWindow(const WindowDescriptor &descriptor);

    struct Impl;
    explicit Window(std::unique_ptr<Impl> implementation);

    std::unique_ptr<Impl> m_implementation;
};

[[nodiscard]] STAR_PLATFORM_API std::expected<std::unique_ptr<Window>, PlatformError>
createWindow(const WindowDescriptor &descriptor);

} // namespace star
