#include "resource_pool.hpp"
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

struct NullObject {
    std::string debugName;
};

struct NullBindGroupLayout {
    std::string debugName;
    std::vector<BindGroupLayoutEntry> entries;
};

using ShaderPool = ResourcePool<NullObject, ShaderModuleHandle>;
using SamplerPool = ResourcePool<NullObject, SamplerHandle>;
using ViewPool = ResourcePool<NullObject, TextureViewHandle>;
using PipelineLayoutPool = ResourcePool<NullObject, PipelineLayoutHandle>;
using BindGroupPool = ResourcePool<NullObject, BindGroupHandle>;
using PipelinePool = ResourcePool<NullObject, GraphicsPipelineHandle>;

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

    void beginRenderPass(const RenderPassBeginInfo &) noexcept override {
        if (!m_submitted) {
            ++m_renderPassCount;
        }
    }

    void endRenderPass() noexcept override {
        if (!m_submitted && m_renderPassCount > 0) {
            --m_renderPassCount;
        }
    }

    void setViewport(float, float, float, float) noexcept override {}
    void setScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) noexcept override {}
    void bindGraphicsPipeline(GraphicsPipelineHandle, std::span<const BindGroupHandle>) noexcept override {}
    void pushConstants(const void *, std::uint32_t) noexcept override {}
    void bindVertexBuffer(BufferHandle, std::uint64_t) noexcept override {}
    void bindIndexBuffer(BufferHandle, std::uint64_t, bool) noexcept override {}

    void draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) noexcept override {
        if (!m_submitted) {
            ++m_drawCount;
        }
    }

    void drawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) noexcept override {
        if (!m_submitted) {
            ++m_drawCount;
        }
    }

    void copyBufferToTexture(BufferHandle, TextureHandle) noexcept override {
        if (!m_submitted) {
            ++m_copyCount;
        }
    }

    void copyTextureToBuffer(TextureHandle, BufferHandle) noexcept override {
        if (!m_submitted) {
            ++m_copyCount;
        }
    }

    void copyBuffer(BufferHandle, BufferHandle, std::uint64_t) noexcept override {
        if (!m_submitted) {
            ++m_copyCount;
        }
    }

  private:
    QueueType m_queue;
    bool m_submitted = false;
    std::uint32_t m_barrierCount = 0;
    std::uint32_t m_debugMarkerCount = 0;
    std::uint32_t m_timestampCount = 0;
    std::uint32_t m_renderPassCount = 0;
    std::uint32_t m_drawCount = 0;
    std::uint32_t m_copyCount = 0;
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

    [[nodiscard]] std::string_view getDeviceName() const noexcept override { return "Null device"; }

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
        if (description.width == 0 || description.height == 0 || description.depth == 0 || description.mipLevels == 0 ||
            description.arrayLayers == 0 || description.usage == TextureUsage::None) {
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

    [[nodiscard]] std::expected<void, DeviceError> writeBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                                               std::uint64_t offset) override {
        std::scoped_lock lock(m_mutex);
        const NullBuffer *target = m_buffers.get(buffer);
        if (target == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (target->desc.memory != MemoryType::Upload) {
            return std::unexpected(DeviceError::Unsupported);
        }
        if (offset > target->desc.size || data.size() > target->desc.size - offset) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, DeviceError> readBuffer(BufferHandle buffer, std::span<std::byte> data,
                                                              std::uint64_t offset) override {
        std::scoped_lock lock(m_mutex);
        const NullBuffer *target = m_buffers.get(buffer);
        if (target == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (target->desc.memory != MemoryType::Readback) {
            return std::unexpected(DeviceError::Unsupported);
        }
        if (offset > target->desc.size || data.size() > target->desc.size - offset) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::fill(data.begin(), data.end(), std::byte{0});
        return {};
    }

    [[nodiscard]] std::expected<ShaderModuleHandle, DeviceError>
    createShaderModule(const ShaderModuleDesc &description) override {
        if (description.spirv.empty()) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        return insertObject<ShaderPool, ShaderModuleHandle>(m_shaders, description.debugName);
    }

    [[nodiscard]] std::expected<SamplerHandle, DeviceError> createSampler(const SamplerDesc &description) override {
        std::scoped_lock lock(m_mutex);
        return insertObject<SamplerPool, SamplerHandle>(m_samplers, description.debugName);
    }

    [[nodiscard]] std::expected<TextureViewHandle, DeviceError>
    createTextureView(const TextureViewDesc &description) override {
        std::scoped_lock lock(m_mutex);
        if (!m_textures.contains(description.texture)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return insertObject<ViewPool, TextureViewHandle>(m_views, description.debugName);
    }

    [[nodiscard]] std::expected<BindGroupLayoutHandle, DeviceError>
    createBindGroupLayout(const BindGroupLayoutDesc &description) override {
        if (description.entries.empty()) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        try {
            NullBindGroupLayout layout;
            layout.debugName = description.debugName;
            layout.entries.assign(description.entries.begin(), description.entries.end());
            return m_bindGroupLayouts.emplace(std::move(layout));
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    [[nodiscard]] std::expected<PipelineLayoutHandle, DeviceError>
    createPipelineLayout(const PipelineLayoutDesc &description) override {
        std::scoped_lock lock(m_mutex);
        for (const BindGroupLayoutHandle layout : description.bind_groups) {
            if (!m_bindGroupLayouts.contains(layout)) {
                return std::unexpected(DeviceError::InvalidHandle);
            }
        }
        return insertObject<PipelineLayoutPool, PipelineLayoutHandle>(m_pipelineLayouts, description.debugName);
    }

    [[nodiscard]] std::expected<BindGroupHandle, DeviceError>
    createBindGroup(const BindGroupDesc &description) override {
        std::scoped_lock lock(m_mutex);
        const NullBindGroupLayout *layout = m_bindGroupLayouts.get(description.layout);
        if (layout == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        for (const BindGroupLayoutEntry &entry : layout->entries) {
            const auto found =
                std::find_if(description.entries.begin(), description.entries.end(),
                             [&entry](const BindGroupEntry &candidate) { return candidate.binding == entry.binding; });
            if (found == description.entries.end()) {
                return std::unexpected(DeviceError::InvalidDescriptor);
            }
            const bool isTexture = entry.type == BindingType::CombinedImageSampler;
            const bool valid = isTexture ? m_views.contains(found->texture_view) && m_samplers.contains(found->sampler)
                                         : m_buffers.contains(found->buffer);
            if (!valid) {
                return std::unexpected(DeviceError::InvalidHandle);
            }
        }
        return insertObject<BindGroupPool, BindGroupHandle>(m_bindGroups, description.debugName);
    }

    [[nodiscard]] std::expected<GraphicsPipelineHandle, DeviceError>
    createGraphicsPipeline(const GraphicsPipelineDesc &description) override {
        if (description.color_formats.empty()) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        if (!m_pipelineLayouts.contains(description.layout) || !m_shaders.contains(description.vertex_shader) ||
            !m_shaders.contains(description.fragment_shader)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return insertObject<PipelinePool, GraphicsPipelineHandle>(m_pipelines, description.debugName);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(BufferHandle buffer) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_buffers.markForDestroy(buffer)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        try {
            m_pendingDestroys.push_back(
                PendingDestroy{PendingType::Buffer, buffer.getIndex(), buffer.getGeneration(), m_frameIndex + 1});
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
            m_pendingDestroys.push_back(
                PendingDestroy{PendingType::Texture, texture.getIndex(), texture.getGeneration(), m_frameIndex + 1});
        } catch (...) {
            m_textures.cancelDestroy(texture);
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(ShaderModuleHandle shader) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_shaders.erase(shader) ? std::expected<void, DeviceError>{}
                                       : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(SamplerHandle sampler) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_samplers.erase(sampler) ? std::expected<void, DeviceError>{}
                                         : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(TextureViewHandle view) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_views.erase(view) ? std::expected<void, DeviceError>{} : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(BindGroupLayoutHandle layout) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_bindGroupLayouts.erase(layout) ? std::expected<void, DeviceError>{}
                                                : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(PipelineLayoutHandle layout) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_pipelineLayouts.erase(layout) ? std::expected<void, DeviceError>{}
                                               : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(BindGroupHandle group) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_bindGroups.erase(group) ? std::expected<void, DeviceError>{}
                                         : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(GraphicsPipelineHandle pipeline) noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_pipelines.erase(pipeline) ? std::expected<void, DeviceError>{}
                                           : std::unexpected(DeviceError::InvalidHandle);
    }

    [[nodiscard]] std::expected<void, DeviceError> destroy(SwapchainHandle) noexcept override {
        return std::unexpected(DeviceError::Unsupported);
    }

    [[nodiscard]] std::expected<SwapchainHandle, DeviceError> createSwapchain(const SwapchainDesc &) override {
        return std::unexpected(DeviceError::Unsupported);
    }

    [[nodiscard]] std::expected<SwapchainInfo, DeviceError> getSwapchainInfo(SwapchainHandle) const override {
        return std::unexpected(DeviceError::Unsupported);
    }

    [[nodiscard]] std::expected<TextureHandle, DeviceError> acquireNextImage(SwapchainHandle) override {
        return std::unexpected(DeviceError::Unsupported);
    }

    [[nodiscard]] std::expected<void, DeviceError> resizeSwapchain(SwapchainHandle, std::uint32_t,
                                                                   std::uint32_t) override {
        return std::unexpected(DeviceError::Unsupported);
    }

    [[nodiscard]] std::expected<void, DeviceError> present(SwapchainHandle) override {
        return std::unexpected(DeviceError::Unsupported);
    }

    [[nodiscard]] std::expected<void, DeviceError> waitIdle() noexcept override { return {}; }

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
    template <typename Pool, typename HandleType>
    std::expected<HandleType, DeviceError> insertObject(Pool &pool, std::string_view name) {
        NullObject object;
        try {
            object.debugName = name;
        } catch (...) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return pool.emplace(std::move(object));
    }

    DeviceCreateInfo m_createInfo;
    Capabilities m_capabilities;
    mutable std::mutex m_mutex;
    ResourcePool<NullBuffer, BufferHandle> m_buffers;
    ResourcePool<NullTexture, TextureHandle> m_textures;
    ResourcePool<NullObject, ShaderModuleHandle> m_shaders;
    ResourcePool<NullObject, SamplerHandle> m_samplers;
    ResourcePool<NullObject, TextureViewHandle> m_views;
    ResourcePool<NullBindGroupLayout, BindGroupLayoutHandle> m_bindGroupLayouts;
    ResourcePool<NullObject, PipelineLayoutHandle> m_pipelineLayouts;
    ResourcePool<NullObject, BindGroupHandle> m_bindGroups;
    ResourcePool<NullObject, GraphicsPipelineHandle> m_pipelines;
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
