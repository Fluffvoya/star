#include "test_support.hpp"

#include "platform/platform.hpp"

namespace {

STAR_TEST(platformCreatesHeadlessWindow) {
    auto platform = star::PlatformContext::initialize();
    STAR_CHECK(platform.has_value());

    star::WindowDescriptor descriptor;
    descriptor.title = "Star Platform Test";
    descriptor.width = 320;
    descriptor.height = 180;

    auto window = star::createWindow(descriptor);
    STAR_CHECK(window.has_value());
    STAR_CHECK(!(*window)->shouldClose());
    STAR_CHECK((*window)->getWidth() == 320);
    STAR_CHECK((*window)->getHeight() == 180);
    STAR_CHECK((*window)->getTitle() == "Star Platform Test");
    STAR_CHECK((*window)->pollEvents().empty());
}

STAR_TEST(frameClockReportsElapsedTime) {
    star::FrameClock clock;
    const double first_tick = clock.tick();
    const double second_tick = clock.tick();

    STAR_CHECK(first_tick >= 0.0);
    STAR_CHECK(second_tick >= 0.0);
}

} // namespace
