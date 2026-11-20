#include "platform/thread.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>
#else
#include <array>
#include <pthread.h>
#endif

namespace star {

std::expected<void, std::error_code> setCurrentThreadName(std::string_view name) noexcept {
    if (name.empty() || name.find('\0') != std::string_view::npos) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

#if defined(_WIN32)
    if (name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(std::make_error_code(std::errc::result_out_of_range));
    }
    const auto byte_count = static_cast<int>(name.size());
    const int wide_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), byte_count, nullptr, 0);
    if (wide_count == 0) {
        return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }

    try {
        std::wstring native_name(static_cast<std::size_t>(wide_count), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), byte_count, native_name.data(),
                                wide_count) == 0) {
            return std::unexpected(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
        }

        const HRESULT result = SetThreadDescription(GetCurrentThread(), native_name.c_str());
        if (FAILED(result)) {
            const auto code =
                static_cast<int>(HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : result);
            return std::unexpected(std::error_code(code, std::system_category()));
        }
    } catch (const std::bad_alloc &) {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
#else
#if defined(__APPLE__)
    std::array<char, 64> native_name{};
#else
    std::array<char, 16> native_name{};
#endif
    if (name.size() >= native_name.size()) {
        return std::unexpected(std::make_error_code(std::errc::result_out_of_range));
    }
    name.copy(native_name.data(), name.size());

#if defined(__APPLE__)
    const int result = pthread_setname_np(native_name.data());
#else
    const int result = pthread_setname_np(pthread_self(), native_name.data());
#endif
    if (result != 0) {
        return std::unexpected(std::error_code(result, std::generic_category()));
    }
#endif
    return {};
}

} // namespace star
