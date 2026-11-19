#include "Platform/Platform.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace star {

PlatformContext::PlatformContext() : m_initialized(true) {}

PlatformContext::PlatformContext(PlatformContext &&other) noexcept
    : m_initialized(std::exchange(other.m_initialized, false)) {}

PlatformContext &PlatformContext::operator=(PlatformContext &&other) noexcept {
    if (this != &other) {
        Shutdown();
        m_initialized = std::exchange(other.m_initialized, false);
    }
    return *this;
}

PlatformContext::~PlatformContext() { Shutdown(); }

std::expected<PlatformContext, PlatformError> PlatformContext::Initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return std::unexpected(PlatformError{SDL_GetError()});
    }
    return PlatformContext();
}

void PlatformContext::Shutdown() {
    if (m_initialized) {
        SDL_Quit();
        m_initialized = false;
    }
}

} // namespace star
