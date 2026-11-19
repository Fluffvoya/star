#pragma once

#include "core/cvar.hpp"
#include "core/export.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace star {

enum class ConfigLayer : std::uint8_t { Engine, Project, Platform, User, CommandLine };

enum class ConfigError : std::uint8_t {
    FileRead,
    InvalidYaml,
    UnsupportedValue,
    DuplicateKey,
    InvalidOverride,
    AllocationFailed
};

struct ConfigLoadError {
    ConfigError code;
    ConfigLayer layer;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct ConfigFiles {
    std::filesystem::path engine;
    std::filesystem::path project;
    std::filesystem::path platform;
    std::filesystem::path user;
};

class STAR_CORE_API Config final {
  public:
    // Empty paths skip a layer. Command-line settings have the form --set=key=value.
    [[nodiscard]] static std::expected<Config, ConfigLoadError>
    load(const ConfigFiles &files, std::span<const std::string_view> arguments = {}) noexcept;

    // The returned view borrows this configuration's storage.
    [[nodiscard]] std::optional<std::string_view> get(std::string_view dotted_key) const noexcept;

    // Apply the CVars mapping as a single update to the registered variables.
    [[nodiscard]] std::expected<void, CVarError> applyCVars() const noexcept;

  private:
    std::map<std::string, std::string, std::less<>> m_values;
};

} // namespace star
