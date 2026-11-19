#include "platform/window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace star {

struct Window::Impl final {
    SDL_Window *handle = nullptr;
    SDL_WindowID id = 0;
    std::string title;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool should_close = false;
    std::vector<WindowEvent> events;

    ~Impl() {
        if (handle != nullptr) {
            SDL_DestroyWindow(handle);
        }
    }
};

Window::Window(std::unique_ptr<Impl> implementation) : m_implementation(std::move(implementation)) {}

Window::~Window() = default;

Window::Window(Window &&) noexcept = default;

Window &Window::operator=(Window &&) noexcept = default;

std::span<const WindowEvent> Window::pollEvents() {
    if (!m_implementation) {
        return {};
    }

    m_implementation->events.clear();

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            m_implementation->should_close = true;
            m_implementation->events.push_back(WindowEvent{WindowEventType::CloseRequested});
            continue;
        }

        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == m_implementation->id) {
            m_implementation->should_close = true;
            m_implementation->events.push_back(WindowEvent{WindowEventType::CloseRequested});
            continue;
        }

        if (event.type == SDL_EVENT_WINDOW_RESIZED && event.window.windowID == m_implementation->id) {
            m_implementation->width = std::max(event.window.data1, 0);
            m_implementation->height = std::max(event.window.data2, 0);
            m_implementation->events.push_back(WindowEvent{
                WindowEventType::Resized,
                static_cast<std::uint32_t>(m_implementation->width),
                static_cast<std::uint32_t>(m_implementation->height),
            });
        }
    }

    return m_implementation->events;
}

std::expected<void, PlatformError> Window::presentSolidColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    if (!m_implementation) {
        return std::unexpected(PlatformError{"Window is not available"});
    }

    SDL_Window *handle = m_implementation->handle;
    SDL_Surface *surface = SDL_GetWindowSurface(handle);
    if (surface == nullptr || !SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, red, green, blue)) ||
        !SDL_UpdateWindowSurface(handle)) {
        return std::unexpected(PlatformError{SDL_GetError()});
    }
    return {};
}

bool Window::shouldClose() const { return !m_implementation || m_implementation->should_close; }

std::int32_t Window::getWidth() const { return m_implementation ? m_implementation->width : 0; }

std::int32_t Window::getHeight() const { return m_implementation ? m_implementation->height : 0; }

std::string_view Window::getTitle() const {
    return m_implementation ? std::string_view(m_implementation->title) : std::string_view{};
}

std::expected<std::unique_ptr<Window>, PlatformError> createWindow(const WindowDescriptor &descriptor) {
    if (descriptor.width <= 0 || descriptor.height <= 0) {
        return std::unexpected(PlatformError{"Window dimensions must be positive"});
    }

    auto implementation = std::make_unique<Window::Impl>();
    implementation->title = descriptor.title;
    implementation->width = descriptor.width;
    implementation->height = descriptor.height;
    implementation->handle =
        SDL_CreateWindow(implementation->title.c_str(), implementation->width, implementation->height,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (implementation->handle == nullptr) {
        return std::unexpected(PlatformError{SDL_GetError()});
    }

    implementation->id = SDL_GetWindowID(implementation->handle);
    return std::make_unique<Window>(Window(std::move(implementation)));
}

} // namespace star
