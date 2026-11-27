#include "asset/asset_manager.hpp"

#include "jobs/jobs.hpp"

#include <atomic>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace star {
namespace detail {

struct AssetLoadEntry {
    std::atomic<AssetState> state{AssetState::Loading};
    JobHandle job;
    std::expected<std::vector<std::byte>, AssetLoadError> result = std::unexpected(AssetLoadError::ReadFailed);
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
        return *m_entry->result;
    }
    return {};
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
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!guid.isValid() || path.empty() || error) {
        return std::unexpected(AssetLoadError::InvalidSource);
    }
    if (!m_sources.try_emplace(guid, absolute).second) {
        return std::unexpected(AssetLoadError::DuplicateGuid);
    }
    return {};
}

std::expected<AssetBlobHandle, AssetLoadError> AssetManager::loadAsync(Guid guid) {
    if (const auto loaded = m_loaded.find(guid); loaded != m_loaded.end()) {
        loaded->second->idle_frames = 0;
        return AssetBlobHandle(loaded->second);
    }
    const auto source = m_sources.find(guid);
    if (source == m_sources.end()) {
        return std::unexpected(AssetLoadError::NotRegistered);
    }
    auto entry = std::make_shared<detail::AssetLoadEntry>();
    m_loaded.emplace(guid, entry);
    try {
        // The manager retains loading entries through completion, including at shutdown.
        entry->job = Jobs::run(
            [pending = entry.get(), path = source->second]() noexcept {
                try {
                    pending->result = readBlob(path);
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

std::expected<AssetBlobHandle, AssetLoadError> AssetManager::load(Guid guid) {
    auto handle = loadAsync(guid);
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
