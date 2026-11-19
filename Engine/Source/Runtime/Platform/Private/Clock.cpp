#include "Platform/Clock.hpp"

#include <chrono>

namespace star {

FrameClock::FrameClock() : m_previous(std::chrono::steady_clock::now()) {}

void FrameClock::Reset() { m_previous = std::chrono::steady_clock::now(); }

double FrameClock::Tick() {
    const TimePoint current = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = current - m_previous;
    m_previous = current;
    return elapsed.count();
}

} // namespace star
