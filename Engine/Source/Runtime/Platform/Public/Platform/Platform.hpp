#pragma once

#include "Platform/Clock.hpp"
#include "Platform/Window.hpp"

#include <expected>
#include <string>

namespace star {

class STAR_PLATFORM_API PlatformContext final {
  public:
    PlatformContext(const PlatformContext &) = delete;
    PlatformContext &operator=(const PlatformContext &) = delete;
    PlatformContext(PlatformContext &&other) noexcept;
    PlatformContext &operator=(PlatformContext &&other) noexcept;
    ~PlatformContext();

    [[nodiscard]] static std::expected<PlatformContext, PlatformError> Initialize();

  private:
    PlatformContext();
    void Shutdown();

    bool m_initialized = false;
};

} // namespace star
