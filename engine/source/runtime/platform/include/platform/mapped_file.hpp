#pragma once

#include "platform/export.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <system_error>

namespace star {

class STAR_PLATFORM_API MappedFile final {
  public:
    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;
    MappedFile(MappedFile &&other) noexcept;
    MappedFile &operator=(MappedFile &&other) noexcept;
    ~MappedFile();

    // Maps a complete regular file for reading. An empty file yields an empty view.
    // The caller must keep the file contents and size stable while the mapping is in use.
    [[nodiscard]] static std::expected<MappedFile, std::error_code> open(const std::filesystem::path &path) noexcept;

    // The byte view remains valid for the lifetime of the mapping.
    [[nodiscard]] std::span<const std::byte> getBytes() const noexcept;

  private:
    MappedFile(void *address, std::size_t size) noexcept;
    void close() noexcept;

    void *m_address;
    std::size_t m_size;
};

} // namespace star
