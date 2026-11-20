#include "core/log.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace star {
namespace {

std::mutex &getLogMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<std::ofstream> &getFileSink() {
    static std::unique_ptr<std::ofstream> sink;
    return sink;
}

[[nodiscard]] const char *levelName(LogLevel level) {
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

LogCategory::LogCategory(std::string_view category_name, LogLevel minimum_level)
    : m_name(category_name), m_minimum_level(minimum_level) {}

bool LogCategory::isEnabled(LogLevel level) const noexcept { return level >= m_minimum_level; }

std::string_view LogCategory::getName() const noexcept { return m_name.getString(); }

namespace log {

std::expected<void, SinkError> setFileSink(const std::filesystem::path &path) noexcept {
    try {
        auto sink = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::app);
        if (!sink->is_open()) {
            return std::unexpected(SinkError::FileOpen);
        }
        std::scoped_lock lock(getLogMutex());
        getFileSink() = std::move(sink);
        return {};
    } catch (...) {
        return std::unexpected(SinkError::FileOpen);
    }
}

void clearFileSink() noexcept {
    std::scoped_lock lock(getLogMutex());
    getFileSink().reset();
}

void flush() noexcept {
    std::scoped_lock lock(getLogMutex());
    std::fflush(stderr);
    if (getFileSink()) {
        getFileSink()->flush();
    }
}

void write(const LogCategory &category, LogLevel level, std::string_view message) {
    const std::string line = std::format("[{}] {}: {}\n", levelName(level), category.getName(), message);

    std::scoped_lock lock(getLogMutex());
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (getFileSink()) {
        getFileSink()->write(line.data(), static_cast<std::streamsize>(line.size()));
        if (level >= LogLevel::Error) {
            getFileSink()->flush();
        }
    }
    if (level >= LogLevel::Error) {
        std::fflush(stderr);
    }
}

} // namespace log

} // namespace star
