#pragma once

#include "Core/Export.hpp"
#include "Core/Name.hpp"

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
    explicit LogCategory(std::string_view categoryName, LogLevel minimumLevel = LogLevel::Info);

    [[nodiscard]] bool IsEnabled(LogLevel level) const noexcept;
    [[nodiscard]] std::string_view GetName() const noexcept;

  private:
    Name m_name;
    LogLevel m_minimumLevel;
};

namespace log {

STAR_CORE_API void Write(const LogCategory &category, LogLevel level, std::string_view message);

template <typename... Args>
void Write(const LogCategory &category, LogLevel level, std::format_string<Args...> format, Args &&...args) {
    if (category.IsEnabled(level)) {
        Write(category, level, std::format(format, std::forward<Args>(args)...));
    }
}

template <typename... Args>
void Trace(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    Write(category, LogLevel::Trace, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Debug(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    Write(category, LogLevel::Debug, format, std::forward<Args>(args)...);
}

template <typename... Args> void Info(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    Write(category, LogLevel::Info, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Warning(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    Write(category, LogLevel::Warning, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Error(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    Write(category, LogLevel::Error, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Fatal(const LogCategory &category, std::format_string<Args...> format, Args &&...args) {
    Write(category, LogLevel::Fatal, format, std::forward<Args>(args)...);
}

} // namespace log

} // namespace star
