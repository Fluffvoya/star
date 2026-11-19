#pragma once

#include "Core/Export.hpp"
#include "Core/Log.hpp"

#include <cstdlib>
#include <format>
#include <string_view>

namespace star::detail {

[[noreturn]] STAR_CORE_API void FailAssertion(std::string_view expression, std::string_view file, unsigned int line,
                                              std::string_view message);

} // namespace star::detail

#if defined(STAR_DEBUG) || defined(STAR_DEVELOPMENT)
#define STAR_ASSERT(expression, ...)                                                                                   \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ::star::detail::FailAssertion(#expression, __FILE__, static_cast<unsigned int>(__LINE__),                  \
                                          std::format(__VA_ARGS__));                                                   \
        }                                                                                                              \
    } while (false)
#else
#define STAR_ASSERT(expression, ...) ((void)0)
#endif

#define STAR_VERIFY(expression, ...)                                                                                   \
    do {                                                                                                               \
        if (!(expression)) {                                                                                           \
            ::star::log::Error(::star::LogCategory("Assert"), "Verification failed: {} ({}:{}): {}", #expression,      \
                               __FILE__, __LINE__, std::format(__VA_ARGS__));                                          \
        }                                                                                                              \
    } while (false)

#define STAR_FATAL(...)                                                                                                \
    do {                                                                                                               \
        ::star::detail::FailAssertion("fatal", __FILE__, static_cast<unsigned int>(__LINE__),                          \
                                      std::format(__VA_ARGS__));                                                       \
    } while (false)
