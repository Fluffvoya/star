#pragma once

#include "asset/export.hpp"
#include "asset/model_asset.hpp"
#include "core/guid.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace star {

template <typename T>
concept DecodedAsset = std::same_as<T, TextureAsset> || std::same_as<T, StaticModelAsset>;

namespace detail {
struct AssetLoadEntry;
enum class AssetKind : std::uint8_t { Blob, Texture, Model };

template <DecodedAsset T>
constexpr AssetKind asset_kind = std::same_as<T, TextureAsset> ? AssetKind::Texture : AssetKind::Model;
} // namespace detail

enum class AssetState : std::uint8_t { Unloaded, Loading, Ready, Failed };

enum class AssetLoadError : std::uint8_t {
    InvalidSource,
    DuplicateGuid,
    NotRegistered,
    ReadFailed,
    TypeMismatch,
    DecodeFailed,
};

class STAR_ASSET_API AssetBlobHandle final {
  public:
    AssetBlobHandle() noexcept = default;

    [[nodiscard]] AssetState getState() const noexcept;
    [[nodiscard]] std::optional<AssetLoadError> getError() const noexcept;
    // Retain a handle while using the returned view. Ready blobs can contain zero bytes.
    [[nodiscard]] std::span<const std::byte> getBytes() const noexcept;

  private:
    friend class AssetManager;
    template <DecodedAsset T> friend class AssetHandle;
    [[nodiscard]] const TextureAsset *getTexture() const noexcept;
    [[nodiscard]] const StaticModelAsset *getModel() const noexcept;
    explicit AssetBlobHandle(std::shared_ptr<detail::AssetLoadEntry> entry) noexcept;

    std::shared_ptr<detail::AssetLoadEntry> m_entry;
};

template <DecodedAsset T> class AssetHandle final {
  public:
    AssetHandle() noexcept = default;

    [[nodiscard]] AssetState getState() const noexcept { return m_storage.getState(); }
    [[nodiscard]] bool isReady() const noexcept { return getState() == AssetState::Ready; }
    [[nodiscard]] std::optional<AssetLoadError> getError() const noexcept { return m_storage.getError(); }

    // The returned pointer remains valid while a handle retains the asset.
    [[nodiscard]] const T *get() const noexcept {
        if constexpr (std::same_as<T, TextureAsset>) {
            return m_storage.getTexture();
        } else {
            return m_storage.getModel();
        }
    }

  private:
    friend class AssetManager;
    explicit AssetHandle(AssetBlobHandle storage) noexcept : m_storage(std::move(storage)) {}

    AssetBlobHandle m_storage;
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

    template <DecodedAsset T>
    [[nodiscard]] std::expected<void, AssetLoadError> registerAsset(Guid guid, const std::filesystem::path &path) {
        return registerSource(guid, path, detail::asset_kind<T>);
    }

    template <DecodedAsset T> [[nodiscard]] std::expected<AssetHandle<T>, AssetLoadError> loadAsync(Guid guid) {
        return loadSourceAsync(guid, detail::asset_kind<T>).transform([](AssetBlobHandle storage) {
            return AssetHandle<T>(std::move(storage));
        });
    }

    template <DecodedAsset T> [[nodiscard]] std::expected<AssetHandle<T>, AssetLoadError> load(Guid guid) {
        return finishLoad(loadSourceAsync(guid, detail::asset_kind<T>)).transform([](AssetBlobHandle storage) {
            return AssetHandle<T>(std::move(storage));
        });
    }

    // Call once per frame. Finalizes pending reads and releases entries after enough idle frames.
    void processCompletedLoads() noexcept;

  private:
    struct Source {
        std::filesystem::path path;
        detail::AssetKind kind;
    };

    [[nodiscard]] std::expected<void, AssetLoadError> registerSource(Guid guid, const std::filesystem::path &path,
                                                                     detail::AssetKind kind);
    [[nodiscard]] std::expected<AssetBlobHandle, AssetLoadError> loadSourceAsync(Guid guid, detail::AssetKind kind);
    [[nodiscard]] static std::expected<AssetBlobHandle, AssetLoadError>
    finishLoad(std::expected<AssetBlobHandle, AssetLoadError> handle);

    std::uint32_t m_retention_frames;
    std::map<Guid, Source> m_sources;
    std::map<Guid, std::shared_ptr<detail::AssetLoadEntry>> m_loaded;
};

// A GUID reference does not retain a loaded asset.
template <DecodedAsset T> struct AssetRef {
    Guid guid;

    [[nodiscard]] std::expected<AssetHandle<T>, AssetLoadError> load(AssetManager &manager) const {
        return manager.load<T>(guid);
    }
    [[nodiscard]] std::expected<AssetHandle<T>, AssetLoadError> loadAsync(AssetManager &manager) const {
        return manager.loadAsync<T>(guid);
    }
};

} // namespace star
