#include "generation_cache.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <random>
#include <sstream>
#include <utility>

namespace star::header_tool {

namespace {

constexpr std::uint64_t initial_hash = 14695981039346656037ull;
constexpr std::string_view cache_format = "star-header-cache-2";

std::uint64_t hashBytes(std::uint64_t hash, std::string_view bytes) {
    for (const char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool dependenciesMatch(const Dependencies &dependencies) {
    for (const auto &[path, hash] : dependencies) {
        if (fingerprintFile(path) != hash) {
            return false;
        }
    }
    return true;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

} // namespace

std::uint64_t fingerprint(std::string_view content) { return hashBytes(initial_hash, content); }

std::optional<std::uint64_t> fingerprintFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    auto hash = initial_hash;
    std::array<char, 8192> buffer;
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() != 0) {
        hash = hashBytes(hash, std::string_view(buffer.data(), static_cast<std::size_t>(file.gcount())));
    }
    return file.eof() && !file.bad() ? std::optional(hash) : std::nullopt;
}

std::uint64_t fingerprintArguments(std::span<const std::string> arguments) {
    auto hash = initial_hash;
    for (const auto &argument : arguments) {
        hash = hashBytes(hash, std::to_string(argument.size()));
        hash = hashBytes(hash, ":");
        hash = hashBytes(hash, argument);
    }
    return hash;
}

std::optional<CachedGeneration> readCache(const std::filesystem::path &path, std::uint64_t key) {
    std::ifstream input(path, std::ios::binary);
    std::string header;
    std::uint64_t cached_key = 0;
    std::size_t count = 0;
    if (!(input >> header >> cached_key >> count) || header != cache_format || cached_key != key || count == 0) {
        return std::nullopt;
    }

    CachedGeneration entry;
    for (std::size_t index = 0; index < count; ++index) {
        std::string file;
        std::uint64_t hash = 0;
        if (!(input >> std::quoted(file) >> hash) ||
            !entry.dependencies.emplace(std::filesystem::path(std::u8string(file.begin(), file.end())), hash).second) {
            return std::nullopt;
        }
    }
    std::uint64_t header_hash = 0;
    std::uint64_t source_hash = 0;
    if (!dependenciesMatch(entry.dependencies) ||
        !(input >> header_hash >> source_hash >> std::quoted(entry.files.header) >> std::quoted(entry.files.source)) ||
        fingerprint(entry.files.header) != header_hash || fingerprint(entry.files.source) != source_hash) {
        return std::nullopt;
    }
    return entry;
}

bool writeCache(const std::filesystem::path &path, std::uint64_t key, const CachedGeneration &entry) {
    if (entry.dependencies.empty() || !dependenciesMatch(entry.dependencies)) {
        return true;
    }
    std::ostringstream output;
    output << cache_format << ' ' << key << ' ' << entry.dependencies.size() << '\n';
    for (const auto &[file, hash] : entry.dependencies) {
        const auto portable = file.generic_u8string();
        output << std::quoted(std::string(portable.begin(), portable.end())) << ' ' << hash << '\n';
    }
    output << fingerprint(entry.files.header) << ' ' << fingerprint(entry.files.source) << '\n'
           << std::quoted(entry.files.header) << '\n'
           << std::quoted(entry.files.source) << '\n';
    return writeIfChanged(path, output.str());
}

bool writeIfChanged(const std::filesystem::path &path, std::string_view contents) {
    std::ifstream existing(path, std::ios::binary);
    if (existing) {
        const std::string data{std::istreambuf_iterator<char>(existing), {}};
        if (data == contents && !existing.bad()) {
            return true;
        }
        existing.close();
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    const std::random_device::result_type nonce = std::random_device{}();
    auto directory = path;
    directory += ".tmp-" + std::to_string(nonce);
    if (!std::filesystem::create_directory(directory)) {
        return false;
    }
    const TemporaryDirectory temporary{std::move(directory)};
    const auto file = temporary.path / "output";
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output << contents;
    output.close();
    if (!output) {
        return false;
    }
    std::filesystem::rename(file, path);
    return true;
}

} // namespace star::header_tool
