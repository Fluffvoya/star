#include "test_support.hpp"

#include "platform/platform.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>

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

STAR_TEST(platformPollsKeyboardAndMouseEvents) {
    auto platform = star::PlatformContext::initialize();
    STAR_CHECK(platform.has_value());
    auto window = star::createWindow(star::WindowDescriptor{"Input Test", 320, 180});
    STAR_CHECK(window.has_value());

    int window_count = 0;
    SDL_Window **windows = SDL_GetWindows(&window_count);
    STAR_CHECK(windows != nullptr && window_count >= 1);
    const SDL_WindowID window_id = SDL_GetWindowID(windows[window_count - 1]);
    SDL_free(windows);

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.windowID = window_id;
    key.key.scancode = SDL_SCANCODE_A;
    key.key.mod = SDL_KMOD_CTRL;
    STAR_CHECK(SDL_PushEvent(&key) == 1);

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = window_id;
    motion.motion.state = SDL_BUTTON_LMASK;
    motion.motion.x = 12.0F;
    motion.motion.y = 34.0F;
    motion.motion.xrel = 2.0F;
    motion.motion.yrel = -1.0F;
    STAR_CHECK(SDL_PushEvent(&motion) == 1);

    const auto events = (*window)->pollEvents();
    STAR_CHECK(events.size() == 2);
    STAR_CHECK(events[0].type == star::WindowEventType::KeyDown);
    STAR_CHECK(events[0].code == SDL_SCANCODE_A && events[0].modifiers == SDL_KMOD_CTRL);
    STAR_CHECK(events[1].type == star::WindowEventType::MouseMotion);
    STAR_CHECK(events[1].x == 12.0F && events[1].yrel == -1.0F);
}

STAR_TEST(frameClockReportsElapsedTime) {
    star::FrameClock clock;
    const double first_tick = clock.tick();
    const double second_tick = clock.tick();

    STAR_CHECK(first_tick >= 0.0);
    STAR_CHECK(second_tick >= 0.0);
}

STAR_TEST(dynamicLibraryOwnsAndReleasesModule) {
    using NextValue = int (*)();
    {
        auto original = star::DynamicLibrary::open(STAR_PLATFORM_TEST_MODULE);
        STAR_CHECK(original.has_value());
        star::DynamicLibrary library(std::move(*original));
        STAR_CHECK(!original->isOpen());

        auto other = star::DynamicLibrary::open(std::filesystem::absolute(STAR_PLATFORM_TEST_MODULE));
        STAR_CHECK(other.has_value());
        *other = std::move(library);
        STAR_CHECK(!library.isOpen());
        STAR_CHECK(other->isOpen());

        const auto symbol = other->findFunction("starTestNextValue");
        STAR_CHECK(symbol.has_value());
        const auto next_value = reinterpret_cast<NextValue>(*symbol);
        STAR_CHECK(next_value() == 1);
        STAR_CHECK(next_value() == 2);
    }

    const auto reopened = star::DynamicLibrary::open(STAR_PLATFORM_TEST_MODULE);
    STAR_CHECK(reopened.has_value());
    const auto symbol = reopened->findFunction("starTestNextValue");
    STAR_CHECK(symbol.has_value());
    STAR_CHECK(reinterpret_cast<NextValue>(*symbol)() == 1);
}

STAR_TEST(dynamicLibraryReportsInvalidPathsAndSymbols) {
    using star::DynamicLibraryErrorCode;
    using namespace std::string_view_literals;

    const auto invalid_path = star::DynamicLibrary::open({});
    STAR_CHECK(!invalid_path.has_value() && invalid_path.error().code == DynamicLibraryErrorCode::InvalidPath);

    const auto truncated_path = star::DynamicLibrary::open("module\0suffix"sv);
    STAR_CHECK(!truncated_path.has_value() && truncated_path.error().code == DynamicLibraryErrorCode::InvalidPath);

    const auto missing_library = star::DynamicLibrary::open("star-nonexistent-test-module.missing");
    STAR_CHECK(!missing_library.has_value() && missing_library.error().code == DynamicLibraryErrorCode::LoadFailed);
    STAR_CHECK(!missing_library.error().message.empty());

    auto library = star::DynamicLibrary::open(STAR_PLATFORM_TEST_MODULE);
    STAR_CHECK(library.has_value());

    const auto missing_symbol = library->findFunction("starTestMissingSymbol");
    STAR_CHECK(!missing_symbol.has_value() && missing_symbol.error().code == DynamicLibraryErrorCode::SymbolNotFound);
    STAR_CHECK(!missing_symbol.error().message.empty());

    const auto invalid_symbol = library->findFunction("starTestNextValue\0suffix"sv);
    STAR_CHECK(!invalid_symbol.has_value() && invalid_symbol.error().code == DynamicLibraryErrorCode::InvalidSymbol);
    STAR_CHECK(library->findFunction("starTestNextValue").has_value());

    const star::DynamicLibrary moved(std::move(*library));
    STAR_CHECK(moved.isOpen());
    const auto closed_symbol = library->findFunction("starTestNextValue");
    STAR_CHECK(!closed_symbol.has_value() && closed_symbol.error().code == DynamicLibraryErrorCode::NotOpen);
}

STAR_TEST(mappedFileReadsBytesAndTransfersOwnership) {
    constexpr std::string_view k_contents = "Star mapping\n";
    const auto expected_bytes = std::as_bytes(std::span(k_contents));

    auto original = star::MappedFile::open("star-platform-map.txt");
    STAR_CHECK(original.has_value());
    const auto original_view = original->getBytes();
    STAR_CHECK(std::ranges::equal(original_view, expected_bytes));

    star::MappedFile moved(std::move(*original));
    STAR_CHECK(original->getBytes().empty());
    STAR_CHECK(moved.getBytes().data() == original_view.data());

    auto replacement = star::MappedFile::open("star-platform-map.txt");
    STAR_CHECK(replacement.has_value());
    *replacement = std::move(moved);
    STAR_CHECK(moved.getBytes().empty());
    STAR_CHECK(replacement->getBytes().data() == original_view.data());
    STAR_CHECK(std::ranges::equal(original_view, expected_bytes));
}

STAR_TEST(mappedFileAcceptsEmptyFilesAndRejectsInvalidInputs) {
    using namespace std::string_view_literals;

    const auto empty = star::MappedFile::open("star-platform-map-empty.txt");
    STAR_CHECK(empty.has_value() && empty->getBytes().empty());

    const auto invalid_path = star::MappedFile::open("star-platform-map.txt\0suffix"sv);
    STAR_CHECK(!invalid_path.has_value() && invalid_path.error() == std::errc::invalid_argument);

    const auto missing_file = star::MappedFile::open("star-platform-map.missing");
    STAR_CHECK(!missing_file.has_value() && missing_file.error() == std::errc::no_such_file_or_directory);

    const auto directory = star::MappedFile::open(".");
    STAR_CHECK(!directory.has_value() && directory.error() == std::errc::operation_not_supported);
}

} // namespace
