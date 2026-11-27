#include "asset-pipeline/derived_data_cache.hpp"

#include <xxhash.h>

#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace star::asset_pipeline {
namespace {

std::string hexKey(const DerivedDataKey &key) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(key.size() * 2);
    for (const std::byte byte : key) {
        const auto value = std::to_integer<unsigned>(byte);
        text.push_back(digits[value >> 4]);
        text.push_back(digits[value & 0x0F]);
    }
    return text;
}

std::span<const std::byte> bytesOf(std::string_view text) { return std::as_bytes(std::span(text)); }

std::unexpected<DerivedDataError> ioFailure(const std::filesystem::path &path) {
    return std::unexpected(DerivedDataError{DerivedDataErrorCode::IoFailure, path});
}

} // namespace

DerivedDataKey fingerprintDerivedDataInput(std::span<const std::byte> bytes) {
    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, XXH3_128bits(bytes.data(), bytes.size()));
    DerivedDataKey key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(canonical.digest[i]);
    }
    return key;
}

std::expected<DerivedDataKey, DerivedDataError> makeDerivedDataKey(std::span<const std::byte> source,
                                                                   const AssetMeta &meta, std::string_view platform,
                                                                   std::span<const DerivedDataKey> dependencies) {
    const auto yaml = writeAssetMeta(meta);
    if (!yaml || platform.empty()) {
        return std::unexpected(DerivedDataError{DerivedDataErrorCode::InvalidInput, {}});
    }

    // Hash the fields separately so their boundaries cannot be ambiguous.
    std::vector<DerivedDataKey> fields{fingerprintDerivedDataInput(bytesOf("star-ddc-v1")),
                                       fingerprintDerivedDataInput(source), fingerprintDerivedDataInput(bytesOf(*yaml)),
                                       fingerprintDerivedDataInput(bytesOf(platform))};
    fields.insert(fields.end(), dependencies.begin(), dependencies.end());
    return fingerprintDerivedDataInput(std::as_bytes(std::span(fields)));
}

DerivedDataCache::DerivedDataCache(std::filesystem::path root) : m_root(std::move(root)) {}

std::expected<std::optional<std::vector<std::byte>>, DerivedDataError>
DerivedDataCache::read(const DerivedDataKey &key) const {
    const auto path = m_root / hexKey(key);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        if (error && error != std::errc::no_such_file_or_directory) {
            return ioFailure(path);
        }
        const auto root_status = std::filesystem::status(m_root, error);
        if (std::filesystem::is_directory(root_status) || error == std::errc::no_such_file_or_directory) {
            return std::nullopt;
        }
        return ioFailure(m_root);
    }
    if (error || !std::filesystem::is_regular_file(status)) {
        return ioFailure(path);
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return ioFailure(path);
    }
    const auto size = static_cast<std::streamoff>(input.tellg());
    if (size < 0 || static_cast<std::uintmax_t>(size) > std::vector<std::byte>().max_size() ||
        size > std::numeric_limits<std::streamsize>::max() || !input.seekg(0)) {
        return ioFailure(path);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return ioFailure(path);
    }
    if (input.peek() != std::char_traits<char>::eof() || input.bad()) {
        return ioFailure(path);
    }
    return bytes;
}

std::expected<void, DerivedDataError> DerivedDataCache::store(const DerivedDataKey &key,
                                                              std::span<const std::byte> bytes) const {
    const auto path = m_root / hexKey(key);
    if (bytes.size() > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return ioFailure(path);
    }
    std::error_code error;
    std::filesystem::create_directories(m_root, error);
    if (error) {
        return ioFailure(m_root);
    }
    auto temporary = path;
    temporary += "." + Guid::generate().toString() + ".tmp";
    std::ofstream output(temporary, std::ios::binary);
    if (!output) {
        return ioFailure(temporary);
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    output.close();
    if (output) {
        std::filesystem::rename(temporary, path, error);
        if (!error) {
            return {};
        }
    }
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    return ioFailure(path);
}

} // namespace star::asset_pipeline
