#pragma once

#include "Platform/Export.hpp"

#include <chrono>

namespace star {

class STAR_PLATFORM_API FrameClock final {
  public:
    FrameClock();

    void Reset();

    // Returns the elapsed time since the previous tick in seconds.
    [[nodiscard]] double Tick();

  private:
    using TimePoint = std::chrono::steady_clock::time_point;

    TimePoint m_previous;
};

} // namespace star
