#include "rhi/rhi.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace star::rhi {
namespace {

template <typename T, typename HandleType> class ResourcePool final {
  public:
    [[nodiscard]] std::expected<HandleType, DeviceError> emplace(T value) noexcept {
        try {
            for (std::uint32_t index = 0; index < m_slots.size(); ++index) {
                Slot &slot = m_slots[index];
                if (!slot.value.has_value() && !slot.pendingDestroy) {
                    slot.value.emplace(std::move(value));
                    return HandleType(index, slot.generation);
                }
            }
            if (m_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(DeviceError::OutOfMemory);
            }
            m_slots.push_back(Slot{});
            Slot &slot = m_slots.back();
            slot.value.emplace(std::move(value));
            return HandleType(static_cast<std::uint32_t>(m_slots.size() - 1), slot.generation);
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    [[nodiscard]] bool contains(HandleType handle) const noexcept {
        const Slot *slot = getSlot(handle);
        return slot != nullptr && slot->value.has_value();
    }

    [[nodiscard]] bool markForDestroy(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value() || slot->pendingDestroy) {
            return false;
        }
        slot->pendingDestroy = true;
        return true;
    }

    bool cancelDestroy(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value() || !slot->pendingDestroy) {
            return false;
        }
        slot->pendingDestroy = false;
        return true;
    }

    bool reclaim(HandleType handle) noexcept {
        Slot *slot = getSlot(handle);
        if (slot == nullptr || !slot->value.has_value() || !slot->pendingDestroy) {
            return false;
        }
        slot->value.reset();
        slot->pendingDestroy = false;
        ++slot->generation;
        if (slot->generation == 0) {
            slot->generation = 1;
        }
        return true;
    }

  private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 1;
        bool pendingDestroy = false;
    };

    [[nodiscard]] Slot *getSlot(HandleType handle) noexcept {
        if (!handle.isValid() || handle.getIndex() >= m_slots.size()) {
            return nullptr;
        }
        Slot &slot = m_slots[handle.getIndex()];
        return slot.generation == handle.getGeneration() ? &slot : nullptr;
    }

    [[nodiscard]] const Slot *getSlot(HandleType handle) const noexcept {
        if (!handle.isValid() || handle.getIndex() >= m_slots.size()) {
            return nullptr;
        }
        const Slot &slot = m_slots[handle.getIndex()];
        return slot.generation == handle.getGeneration() ? &slot : nullptr;
    }

    std::vector<Slot> m_slots;
};

struct NullBuffer {
    explicit NullBuffer(BufferDesc value) : desc(std::move(value)) {}
    BufferDesc desc;
    std::string debugName;
};

struct NullTexture {
    explicit NullTexture(TextureDesc value) : desc(std::move(value)) {}
    TextureDesc desc;
    std::string debugName;
};

class NullCommandList final : public CommandList {
  public:
    explicit NullCommandList(QueueType queue) noexcept : m_queue(queue) {}

    [[nodiscard]] QueueType getQueue() const noexcept { return m_queue; }
    [[nodiscard]] bool isSubmitted() const noexcept { return m_submitted; }
    void markSubmitted() noexcept { m_submitted = true; }

    void barrier(std::span<const BufferBarrier>, std::span<const TextureBarrier>) noexcept override {
        if (!m_submitted) {
            ++m_barrierCount;
        }
    }

    void beginDebugMarker(std::string_view) noexcept override {
        if (!m_submitted) {
            ++m_debugMarkerCount;
        }
    }

    void endDebugMarker() noexcept override {
        if (!m_submitted && m_debugMarkerCount > 0) {
            --m_debugMarkerCount;
        }
    }

    void writeTimestamp(std::uint32_t) noexcept override {
        if (!m_submitted) {
            ++m_timestampCount;
        }
    }

  private:
    QueueType m_queue;
    bool m_submitted = false;
    std::uint32_t m_barrierCount = 0;
    std::uint32_t m_debugMarkerCount = 0;
    std::uint32_t m_timestampCount = 0;
};

enum class PendingType : std::uint8_t { Buffer, Texture };

struct PendingDestroy {
    PendingType type;
    std::uint32_t index;
    std::uint32_t generation;
    std::uint64_t retireFrame;
};

class NullDevice final : public Device {
  public:
    explicit NullDevice(DeviceCreateInfo createInfo) noexcept : m_createInfo(createInfo) {
        m_capabilities.supportsComputeQueue = true;
        m_capabilities.supportsCopyQueue = true;
        m_capabilities.supportsTimestampQueries = true;
        m_capabilities.supportsDebugMarkers = true;
        m_capabilities.supportsValidation = createInfo.enableValidation;
    }

    [[nodiscard]] const Capabilities &getCapabilities() const noexcept override { return m_capabilities; }

    [[nodiscard]] std::expected<BufferHandle, DeviceError> createBuffer(const BufferDesc &description) override {
        if (description.size == 0 || description.usage == BufferUsage::None) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        NullBuffer buffer(description);
        try {
            buffer.debugName = description.debugName;
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return m_buffers.emplace(std::move(buffer));
    }

    [[nodiscard]] std::expected<TextureHandle, DeviceError> createTexture(const TextureDesc &description) override {
        if (description.width == 0 || description.height == 0 || description.depth == 0 ||
            description.mipLevels == 0 || description.arrayLayers == 0 || description.usage == TextureUsage::None) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        NullTexture texture(description);
        try {
            texture.debugName = description.debugName;
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return m_textures.emplace(std::move(texture));
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(BufferHandle buffer) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_buffers.markForDestroy(buffer)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        try {
            m_pendingDestroys.push_back(PendingDestroy{PendingType::Buffer, buffer.getIndex(), buffer.getGeneration(),
                                                       m_frameIndex + 1});
        } catch (...) {
            m_buffers.cancelDestroy(buffer);
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(TextureHandle texture) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_textures.markForDestroy(texture)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        try {
            m_pendingDestroys.push_back(PendingDestroy{PendingType::Texture, texture.getIndex(), texture.getGeneration(),
                                                       m_frameIndex + 1});
        } catch (...) {
            m_textures.cancelDestroy(texture);
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return {};
    }

    [[nodiscard]] bool isAlive(BufferHandle buffer) const noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_buffers.contains(buffer);
    }

    [[nodiscard]] bool isAlive(TextureHandle texture) const noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_textures.contains(texture);
    }

    [[nodiscard]] std::expected<CommandList *, DeviceError> beginCommandList(QueueType queue) override {
        std::scoped_lock lock(m_mutex);
        try {
            m_commandLists.push_back(std::make_unique<NullCommandList>(queue));
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return m_commandLists.back().get();
    }

    [[nodiscard]] std::expected<SubmitToken, DeviceError>
    submit(QueueType queue, std::span<CommandList *const> commandLists, std::span<const SyncPoint>) override {
        std::scoped_lock lock(m_mutex);
        for (CommandList *commandList : commandLists) {
            auto *nullList = dynamic_cast<NullCommandList *>(commandList);
            if (nullList == nullptr || nullList->getQueue() != queue || nullList->isSubmitted()) {
                return std::unexpected(DeviceError::InvalidState);
            }
        }
        for (CommandList *commandList : commandLists) {
            static_cast<NullCommandList *>(commandList)->markSubmitted();
        }
        return SubmitToken{++m_submitValue};
    }

    void beginFrame() noexcept override {
        std::scoped_lock lock(m_mutex);
        ++m_frameIndex;
        std::erase_if(m_pendingDestroys, [this](const PendingDestroy &pending) {
            if (pending.retireFrame > m_frameIndex) {
                return false;
            }
            if (pending.type == PendingType::Buffer) {
                return m_buffers.reclaim(BufferHandle(pending.index, pending.generation));
            }
            return m_textures.reclaim(TextureHandle(pending.index, pending.generation));
        });
        m_commandLists.clear();
    }

    void endFrame() noexcept override {}

  private:
    DeviceCreateInfo m_createInfo;
    Capabilities m_capabilities;
    mutable std::mutex m_mutex;
    ResourcePool<NullBuffer, BufferHandle> m_buffers;
    ResourcePool<NullTexture, TextureHandle> m_textures;
    std::vector<PendingDestroy> m_pendingDestroys;
    std::vector<std::unique_ptr<NullCommandList>> m_commandLists;
    std::uint64_t m_frameIndex = 0;
    std::uint64_t m_submitValue = 0;
};

} // namespace

std::expected<std::unique_ptr<Device>, DeviceError> createNullDevice(const DeviceCreateInfo &createInfo) {
    if (createInfo.framesInFlight == 0) {
        return std::unexpected(DeviceError::InvalidDescriptor);
    }
    try {
        return std::unique_ptr<Device>(new NullDevice(createInfo));
    } catch (...) {
        return std::unexpected(DeviceError::OutOfMemory);
    }
}

} // namespace star::rhi
