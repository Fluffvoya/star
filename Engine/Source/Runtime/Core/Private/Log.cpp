#include "Core/Log.hpp"

#include <cstdio>
#include <mutex>
#include <string>

namespace star {
namespace {

std::mutex &GetLogMutex() {
    static std::mutex mutex;
    return mutex;
}

[[nodiscard]] const char *LevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Fatal:
        return "fatal";
    }
    return "unknown";
}

} // namespace

LogCategory::LogCategory(std::string_view categoryName, LogLevel minimumLevel)
    : m_name(categoryName), m_minimumLevel(minimumLevel) {}

bool LogCategory::IsEnabled(LogLevel level) const noexcept { return level >= m_minimumLevel; }

std::string_view LogCategory::GetName() const noexcept { return m_name.GetString(); }

namespace log {

void Write(const LogCategory &category, LogLevel level, std::string_view message) {
    const std::string line = std::format("[{}] {}: {}\n", LevelName(level), category.GetName(), message);

    std::scoped_lock lock(GetLogMutex());
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (level >= LogLevel::Error) {
        std::fflush(stderr);
    }
}

} // namespace log

} // namespace star
