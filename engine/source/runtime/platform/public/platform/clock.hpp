#pragma once

#include "platform/export.hpp"

#include <chrono>

namespace star {

class STAR_PLATFORM_API FrameClock final {
  public:
    FrameClock();

    void reset();

    // Returns the elapsed time since the previous tick in seconds.
    [[nodiscard]] double tick();

  private:
    using TimePoint = std::chrono::steady_clock::time_point;

    TimePoint m_previous;
};

} // namespace star
