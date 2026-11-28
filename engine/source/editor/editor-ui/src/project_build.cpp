#include "project_build.hpp"

#include "core/log.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <thread>

namespace star::editor {
namespace {

const LogCategory k_log_category("CMake", LogLevel::Info);

} // namespace

ProjectBuild::~ProjectBuild() {
    while (isRunning() && !poll()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::expected<void, std::string> ProjectBuild::start(const std::filesystem::path &directory,
                                                     std::string_view configuration) {
    if (isRunning()) {
        return std::unexpected("A build is already in progress");
    }

    std::ifstream cache(directory / "CMakeCache.txt");
    std::string line;
    bool ninja = false;
    while (std::getline(cache, line)) {
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        if (line == "CMAKE_GENERATOR:INTERNAL=Ninja" || line == "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config") {
            ninja = true;
            break;
        }
    }
    if (!ninja) {
        return std::unexpected("Choose a directory configured with CMake and Ninja");
    }

    const auto build_path = directory.u8string();
    const std::string config(configuration);
    const char *args[] = {"cmake",
                          "--build",
                          reinterpret_cast<const char *>(build_path.c_str()),
                          configuration.empty() ? nullptr : "--config",
                          config.c_str(),
                          nullptr};
    const auto properties = SDL_CreateProperties();
    if (!properties) {
        return std::unexpected(SDL_GetError());
    }
    const bool ready =
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    if (ready) {
        m_process.reset(SDL_CreateProcessWithProperties(properties));
    }
    const std::string error = m_process ? "" : SDL_GetError();
    SDL_DestroyProperties(properties);
    if (!m_process) {
        return std::unexpected(error);
    }

    m_pending_line.clear();
    log::info(k_log_category, "Building {}", directory.string());
    return {};
}

void ProjectBuild::readOutput() {
    constexpr std::size_t chunk_size = 4096;
    auto *output = SDL_GetProcessOutput(m_process.get());
    if (!output) {
        return;
    }
    std::array<char, chunk_size> buffer{};
    for (int chunk = 0; chunk < 16; ++chunk) {
        const auto bytes = SDL_ReadIO(output, buffer.data(), buffer.size());
        if (bytes == 0) {
            break;
        }
        m_pending_line.append(buffer.data(), bytes);
        std::size_t consumed = 0;
        for (auto end = m_pending_line.find_first_of("\r\n"); end != std::string::npos;
             end = m_pending_line.find_first_of("\r\n", consumed)) {
            if (end > consumed) {
                log::info(k_log_category, "{}", std::string_view(m_pending_line).substr(consumed, end - consumed));
            }
            consumed = end + 1;
        }
        m_pending_line.erase(0, consumed);
        if (m_pending_line.size() >= chunk_size) {
            log::info(k_log_category, "{}", m_pending_line);
            m_pending_line.clear();
        }
    }
}

std::optional<int> ProjectBuild::poll() {
    if (!m_process) {
        return std::nullopt;
    }
    readOutput();
    int status = 0;
    if (!SDL_WaitProcess(m_process.get(), false, &status)) {
        return std::nullopt;
    }
    readOutput();
    if (!m_pending_line.empty()) {
        log::info(k_log_category, "{}", m_pending_line);
        m_pending_line.clear();
    }
    m_process.reset();
    return status;
}

} // namespace star::editor
