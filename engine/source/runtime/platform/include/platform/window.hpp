#pragma once

#include "platform/export.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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
    // `code` holds the Unicode codepoint.
    TextInput,
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
    bool vulkan = false;
};

class STAR_PLATFORM_API Window final {
  public:
    ~Window();

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    Window(Window &&) noexcept;
    Window &operator=(Window &&) noexcept;

    [[nodiscard]] std::span<const WindowEvent> pollEvents();
    // The native SDL_Event pointer is borrowed for the callback. Includes all polled windows.
    using NativeEventObserver = void (*)(const void *event, void *context);
    [[nodiscard]] std::span<const WindowEvent> pollEvents(NativeEventObserver observer, void *context);
    [[nodiscard]] std::expected<void, PlatformError> startTextInput();
    // Present an RGB frame using the window surface on the main thread.
    [[nodiscard]] std::expected<void, PlatformError> presentSolidColor(std::uint8_t red, std::uint8_t green,
                                                                       std::uint8_t blue);
    // SDL_Window* backing this window. Valid until the window is destroyed.
    [[nodiscard]] void *getNativeHandle() const noexcept;
    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] std::int32_t getWidth() const;
    [[nodiscard]] std::int32_t getHeight() const;
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> getPixelSize() const noexcept;
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
