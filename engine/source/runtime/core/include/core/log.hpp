#pragma once

#include "core/export.hpp"
#include "core/name.hpp"

#include <expected>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace star {

enum class LogLevel : unsigned char {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

class STAR_CORE_API LogCategory final {
  public:
    explicit LogCategory(std::string_view category_name, LogLevel minimum_level = LogLevel::Info);

    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;
    [[nodiscard]] std::string_view getName() const noexcept;

  private:
    Name m_name;
    LogLevel m_minimum_level;
};

namespace log {

enum class SinkError : unsigned char { FileOpen };

STAR_CORE_API std::expected<void, SinkError> setFileSink(const std::filesystem::path &path) noexcept;
STAR_CORE_API void clearFileSink() noexcept;
STAR_CORE_API void flush() noexcept;

STAR_CORE_API void write(const LogCategory &category, LogLevel level, std::string_view message);

template <typename... Args>
void write(const LogCategory &category, LogLevel level, std::format_string<Args...> format, Args &&...args) {
    if (category.isEnabled(level)) {
        write(category, level, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void trace(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    write(category, LogLevel::Trace, format, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    write(category, LogLevel::Debug, format, std::forward<Args>(args)...);
}

template <typename... Args> void info(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    write(category, LogLevel::Info, format, std::forward<Args>(args)...);
}

template <typename... Args>
void warning(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    write(category, LogLevel::Warning, format, std::forward<Args>(args)...);
}

template <typename... Args>
void error(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    write(category, LogLevel::Error, format, std::forward<Args>(args)...);
}

template <typename... Args>
void fatal(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    write(category, LogLevel::Fatal, format, std::forward<Args>(args)...);
}

} // namespace log

} // namespace star
