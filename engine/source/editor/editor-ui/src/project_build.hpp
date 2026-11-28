#pragma once

#include <SDL3/SDL_process.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace star::editor {

class ProjectBuild final {
  public:
    ~ProjectBuild();
    [[nodiscard]] std::expected<void, std::string> start(const std::filesystem::path &directory,
                                                         std::string_view configuration);
    [[nodiscard]] std::optional<int> poll();
    [[nodiscard]] bool isRunning() const noexcept { return m_process != nullptr; }

  private:
    void readOutput();

    std::unique_ptr<SDL_Process, decltype(&SDL_DestroyProcess)> m_process{nullptr, &SDL_DestroyProcess};
    std::string m_pending_line;
};

} // namespace star::editor
