#pragma once

#include "platform/clock.hpp"
#include "platform/window.hpp"

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

    [[nodiscard]] static std::expected<PlatformContext, PlatformError> initialize();

  private:
    PlatformContext();
    void shutdown();

    bool m_initialized = false;
};

} // namespace star
