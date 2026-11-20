#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace star::header_tool {

using Dependencies = std::map<std::filesystem::path, std::uint64_t>;

struct GeneratedFiles {
    std::string header;
    std::string source;
};

struct CachedGeneration {
    Dependencies dependencies;
    GeneratedFiles files;
};

std::uint64_t fingerprint(std::string_view content);
std::optional<std::uint64_t> fingerprintFile(const std::filesystem::path &path);
std::uint64_t fingerprintArguments(std::span<const std::string> arguments);
std::optional<CachedGeneration> readCache(const std::filesystem::path &path, std::uint64_t key);
bool writeCache(const std::filesystem::path &path, std::uint64_t key, const CachedGeneration &entry);
bool writeIfChanged(const std::filesystem::path &path, std::string_view contents);

} // namespace star::header_tool
