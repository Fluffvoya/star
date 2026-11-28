#include "core/log.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

struct SinkEntry {
    log::LogSinkId id;
    log::LogSinkFn function;
    void *user_data;
};

std::vector<SinkEntry> &getSinks() {
    static std::vector<SinkEntry> sinks;
    return sinks;
}

log::LogSinkId &getNextSinkId() {
    static log::LogSinkId next = 1;
    return next;
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

LogSinkId addSink(LogSinkFn sink, void *user_data) noexcept {
    if (sink == nullptr) {
        return 0;
    }
    std::scoped_lock lock(getLogMutex());
    const LogSinkId id = getNextSinkId()++;
    try {
        getSinks().push_back(SinkEntry{id, sink, user_data});
    } catch (...) {
        return 0;
    }
    return id;
}

void removeSink(LogSinkId id) noexcept {
    std::scoped_lock lock(getLogMutex());
    auto &sinks = getSinks();
    sinks.erase(std::remove_if(sinks.begin(), sinks.end(), [id](const SinkEntry &entry) { return entry.id == id; }),
                sinks.end());
}

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

    std::vector<SinkEntry> sinks;
    {
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
        if (!getSinks().empty()) {
            try {
                sinks = getSinks();
            } catch (...) {
                return;
            }
        }
    }

    if (sinks.empty()) {
        return;
    }
    const LogRecord record{category.getName(), level, message};
    for (const SinkEntry &sink : sinks) {
        try {
            sink.function(record, sink.user_data);
        } catch (...) {
            // A failing sink must not take the logging path down.
        }
    }
}

} // namespace log

} // namespace star
