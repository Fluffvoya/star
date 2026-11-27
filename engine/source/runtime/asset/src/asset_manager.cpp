#include "asset/asset_manager.hpp"

#include "jobs/jobs.hpp"

#include <atomic>
#include <fstream>
#include <limits>
#include <utility>
#include <variant>
#include <vector>

namespace star {
namespace detail {

using AssetPayload = std::variant<std::vector<std::byte>, TextureAsset, StaticModelAsset>;

struct AssetLoadEntry {
    std::atomic<AssetState> state{AssetState::Loading};
    JobHandle job;
    std::expected<AssetPayload, AssetLoadError> result = std::unexpected(AssetLoadError::ReadFailed);
    std::uint32_t idle_frames = 0;

    void publish() noexcept { state.store(result ? AssetState::Ready : AssetState::Failed, std::memory_order_release); }
};

} // namespace detail
namespace {

std::expected<std::vector<std::byte>, AssetLoadError> readBlob(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return std::unexpected(AssetLoadError::ReadFailed);
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected(AssetLoadError::ReadFailed);
    }
    const auto size = static_cast<std::streamoff>(input.tellg());
    if (size < 0 || static_cast<std::uintmax_t>(size) > std::vector<std::byte>().max_size() ||
        size > std::numeric_limits<std::streamsize>::max() || !input.seekg(0)) {
        return std::unexpected(AssetLoadError::ReadFailed);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return std::unexpected(AssetLoadError::ReadFailed);
    }
    if (input.peek() != std::char_traits<char>::eof() || input.bad()) {
        return std::unexpected(AssetLoadError::ReadFailed);
    }
    return bytes;
}

std::expected<detail::AssetPayload, AssetLoadError> readAsset(const std::filesystem::path &path,
                                                              detail::AssetKind kind) {
    auto bytes = readBlob(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    switch (kind) {
    case detail::AssetKind::Blob:
        return std::move(*bytes);
    case detail::AssetKind::Texture:
        if (auto texture = readTextureAsset(*bytes)) {
            return std::move(*texture);
        }
        break;
    case detail::AssetKind::Model:
        if (auto model = readModelAsset(*bytes)) {
            return std::move(*model);
        }
        break;
    }
    return std::unexpected(AssetLoadError::DecodeFailed);
}

} // namespace

AssetBlobHandle::AssetBlobHandle(std::shared_ptr<detail::AssetLoadEntry> entry) noexcept : m_entry(std::move(entry)) {}

AssetState AssetBlobHandle::getState() const noexcept {
    return m_entry ? m_entry->state.load(std::memory_order_acquire) : AssetState::Unloaded;
}

std::optional<AssetLoadError> AssetBlobHandle::getError() const noexcept {
    if (getState() == AssetState::Failed) {
        return m_entry->result.error();
    }
    return std::nullopt;
}

std::span<const std::byte> AssetBlobHandle::getBytes() const noexcept {
    if (getState() == AssetState::Ready) {
        if (const auto *bytes = std::get_if<std::vector<std::byte>>(&*m_entry->result)) {
            return *bytes;
        }
    }
    return {};
}

const TextureAsset *AssetBlobHandle::getTexture() const noexcept {
    return getState() == AssetState::Ready ? std::get_if<TextureAsset>(&*m_entry->result) : nullptr;
}

const StaticModelAsset *AssetBlobHandle::getModel() const noexcept {
    return getState() == AssetState::Ready ? std::get_if<StaticModelAsset>(&*m_entry->result) : nullptr;
}

AssetManager::AssetManager(std::uint32_t retention_frames) : m_retention_frames(retention_frames) {}

AssetManager::~AssetManager() {
    for (auto &[guid, entry] : m_loaded) {
        if (entry->state.load(std::memory_order_relaxed) == AssetState::Loading) {
            entry->job.wait();
            entry->publish();
        }
    }
}

std::expected<void, AssetLoadError> AssetManager::registerBlob(Guid guid, const std::filesystem::path &path) {
    return registerSource(guid, path, detail::AssetKind::Blob);
}

std::expected<void, AssetLoadError> AssetManager::registerSource(Guid guid, const std::filesystem::path &path,
                                                                 detail::AssetKind kind) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!guid.isValid() || path.empty() || error) {
        return std::unexpected(AssetLoadError::InvalidSource);
    }
    if (!m_sources.try_emplace(guid, Source{absolute, kind}).second) {
        return std::unexpected(AssetLoadError::DuplicateGuid);
    }
    return {};
}

std::expected<AssetBlobHandle, AssetLoadError> AssetManager::loadAsync(Guid guid) {
    return loadSourceAsync(guid, detail::AssetKind::Blob);
}

std::expected<AssetBlobHandle, AssetLoadError> AssetManager::loadSourceAsync(Guid guid, detail::AssetKind kind) {
    const auto source = m_sources.find(guid);
    if (source == m_sources.end()) {
        return std::unexpected(AssetLoadError::NotRegistered);
    }
    if (source->second.kind != kind) {
        return std::unexpected(AssetLoadError::TypeMismatch);
    }
    if (const auto loaded = m_loaded.find(guid); loaded != m_loaded.end()) {
        loaded->second->idle_frames = 0;
        return AssetBlobHandle(loaded->second);
    }
    auto entry = std::make_shared<detail::AssetLoadEntry>();
    m_loaded.emplace(guid, entry);
    try {
        // The manager retains loading entries through completion, including at shutdown.
        entry->job = Jobs::run(
            [pending = entry.get(), path = source->second.path, kind]() noexcept {
                try {
                    pending->result = readAsset(path, kind);
                } catch (...) {
                    pending->result = std::unexpected(AssetLoadError::ReadFailed);
                }
            },
            JobPriority::Background);
    } catch (...) {
        m_loaded.erase(guid);
        return std::unexpected(AssetLoadError::ReadFailed);
    }
    return AssetBlobHandle(std::move(entry));
}

std::expected<AssetBlobHandle, AssetLoadError> AssetManager::load(Guid guid) { return finishLoad(loadAsync(guid)); }

std::expected<AssetBlobHandle, AssetLoadError>
AssetManager::finishLoad(std::expected<AssetBlobHandle, AssetLoadError> handle) {
    if (!handle) {
        return handle;
    }
    auto &entry = *handle->m_entry;
    if (handle->getState() == AssetState::Loading) {
        entry.job.wait();
        entry.publish();
    }
    if (const auto error = handle->getError()) {
        return std::unexpected(*error);
    }
    return handle;
}

void AssetManager::processCompletedLoads() noexcept {
    for (auto it = m_loaded.begin(); it != m_loaded.end();) {
        const auto &entry = it->second;
        if (entry->state.load(std::memory_order_relaxed) == AssetState::Loading) {
            if (!entry->job.isComplete()) {
                ++it;
                continue;
            }
            entry->publish();
        }
        if (entry.use_count() > 1) {
            entry->idle_frames = 0;
        } else if (m_retention_frames == 0 || ++entry->idle_frames >= m_retention_frames) {
            it = m_loaded.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace star
