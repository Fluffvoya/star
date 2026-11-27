#pragma once

#include "asset/export.hpp"
#include "core/guid.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>

namespace star {

namespace detail {
struct AssetLoadEntry;
}

enum class AssetState : std::uint8_t { Unloaded, Loading, Ready, Failed };

enum class AssetLoadError : std::uint8_t { InvalidSource, DuplicateGuid, NotRegistered, ReadFailed };

class STAR_ASSET_API AssetBlobHandle final {
  public:
    AssetBlobHandle() noexcept = default;

    [[nodiscard]] AssetState getState() const noexcept;
    [[nodiscard]] std::optional<AssetLoadError> getError() const noexcept;
    // Retain a handle while using the returned view. Ready blobs can contain zero bytes.
    [[nodiscard]] std::span<const std::byte> getBytes() const noexcept;

  private:
    friend class AssetManager;
    explicit AssetBlobHandle(std::shared_ptr<detail::AssetLoadEntry> entry) noexcept;

    std::shared_ptr<detail::AssetLoadEntry> m_entry;
};

// Manager methods are confined to the owning thread. Handle copies can read published data on any thread.
class STAR_ASSET_API AssetManager final {
  public:
    explicit AssetManager(std::uint32_t retention_frames = 2);
    ~AssetManager();

    AssetManager(const AssetManager &) = delete;
    AssetManager &operator=(const AssetManager &) = delete;

    [[nodiscard]] std::expected<void, AssetLoadError> registerBlob(Guid guid, const std::filesystem::path &path);
    [[nodiscard]] std::expected<AssetBlobHandle, AssetLoadError> loadAsync(Guid guid);
    [[nodiscard]] std::expected<AssetBlobHandle, AssetLoadError> load(Guid guid);

    // Call once per frame. Finalizes pending reads and releases entries after enough idle frames.
    void processCompletedLoads() noexcept;

  private:
    std::uint32_t m_retention_frames;
    std::map<Guid, std::filesystem::path> m_sources;
    std::map<Guid, std::shared_ptr<detail::AssetLoadEntry>> m_loaded;
};

} // namespace star
