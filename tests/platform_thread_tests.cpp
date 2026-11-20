#include "test_support.hpp"

#include "platform/thread.hpp"

#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <array>
#include <pthread.h>
#endif

namespace {

STAR_TEST(platformNamesCurrentWorkerThread) {
    bool matches = false;
    std::jthread worker([&matches] {
        if (!star::setCurrentThreadName("star-test")) {
            return;
        }

#if defined(_WIN32)
        wchar_t *name = nullptr;
        if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &name)) && name != nullptr) {
            matches = std::wstring_view(name) == L"star-test";
            LocalFree(name);
        }
#else
        std::array<char, 64> name{};
        matches = pthread_getname_np(pthread_self(), name.data(), name.size()) == 0 &&
                  std::string_view(name.data()) == "star-test";
#endif
    });
    worker.join();
    STAR_CHECK(matches);
}

STAR_TEST(platformRejectsInvalidThreadNames) {
    using namespace std::string_view_literals;

    const auto empty = star::setCurrentThreadName("");
    STAR_CHECK(!empty && empty.error() == std::errc::invalid_argument);

    const auto embedded_null = star::setCurrentThreadName("star\0test"sv);
    STAR_CHECK(!embedded_null && embedded_null.error() == std::errc::invalid_argument);

#if !defined(_WIN32)
    constexpr std::string_view k_long_name = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
    const auto too_long = star::setCurrentThreadName(k_long_name);
    STAR_CHECK(!too_long && too_long.error() == std::errc::result_out_of_range);
#endif
}

STAR_TEST(platformRejectsEmptyThreadAffinity) {
    const auto result = star::setCurrentThreadAffinity({});
    STAR_CHECK(!result && result.error() == std::errc::invalid_argument);
}

} // namespace
