#include "TestSupport.hpp"

#include "Platform/Platform.hpp"

namespace {

STAR_TEST(PlatformCreatesHeadlessWindow) {
    auto platform = star::PlatformContext::Initialize();
    STAR_CHECK(platform.has_value());

    star::WindowDescriptor descriptor;
    descriptor.title = "Star Platform Test";
    descriptor.width = 320;
    descriptor.height = 180;

    auto window = star::CreateWindow(descriptor);
    STAR_CHECK(window.has_value());
    STAR_CHECK(!(*window)->ShouldClose());
    STAR_CHECK((*window)->GetWidth() == 320);
    STAR_CHECK((*window)->GetHeight() == 180);
    STAR_CHECK((*window)->GetTitle() == "Star Platform Test");
    STAR_CHECK((*window)->PollEvents().empty());
}

STAR_TEST(FrameClockReportsElapsedTime) {
    star::FrameClock clock;
    const double firstTick = clock.Tick();
    const double secondTick = clock.Tick();

    STAR_CHECK(firstTick >= 0.0);
    STAR_CHECK(secondTick >= 0.0);
}

} // namespace
