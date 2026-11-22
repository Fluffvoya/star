#include "rhi/vulkan.hpp"

#ifdef STAR_WITH_VULKAN
#include "resource_pool.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <bit>
#include <bitset>
#include <mutex>
#include <utility>
#include <vector>

namespace star::rhi {
namespace {

struct VulkanBuffer {
    explicit VulkanBuffer(VmaAllocator owner) : allocator(owner) {}
    ~VulkanBuffer() {
        if (buffer) {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }
    VmaAllocator allocator;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct VulkanTexture {
    explicit VulkanTexture(VmaAllocator owner) : allocator(owner) {}
    ~VulkanTexture() {
        if (image) {
            vmaDestroyImage(allocator, image, allocation);
        }
    }
    VmaAllocator allocator;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageSubresourceRange range{};
};

using BufferPool = ResourcePool<std::unique_ptr<VulkanBuffer>, BufferHandle>;
using TexturePool = ResourcePool<std::unique_ptr<VulkanTexture>, TextureHandle>;

class VulkanCommandList final : public CommandList {
  public:
    VulkanCommandList(const VulkanRuntime &runtime, BufferPool &buffers, TexturePool &textures, std::mutex &mutex,
                      QueueType queue)
        : m_runtime(runtime), m_buffers(buffers), m_textures(textures), m_mutex(mutex), m_queue(queue) {}

    ~VulkanCommandList() override {
        if (m_pool) {
            m_runtime.vk.vkDestroyCommandPool(m_runtime.device, m_pool, nullptr);
        }
        if (m_queries) {
            m_runtime.vk.vkDestroyQueryPool(m_runtime.device, m_queries, nullptr);
        }
    }

    [[nodiscard]] std::expected<void, DeviceError> initialize() noexcept {
        const auto &vk = m_runtime.vk;
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = m_runtime.queueFamily;
        VkResult result = vk.vkCreateCommandPool(m_runtime.device, &pool, nullptr, &m_pool);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = m_pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        result = vk.vkAllocateCommandBuffers(m_runtime.device, &allocation, &m_commands);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vk.vkBeginCommandBuffer(m_commands, &begin);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        if (m_runtime.queueProperties.timestampValidBits != 0) {
            VkQueryPoolCreateInfo queries{};
            queries.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queries.queryCount = query_count;
            result = vk.vkCreateQueryPool(m_runtime.device, &queries, nullptr, &m_queries);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            vk.vkCmdResetQueryPool(m_commands, m_queries, 0, query_count);
        }
        return {};
    }

    void barrier(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        try {
            for (const BufferBarrier &buffer : buffers) {
                if (!m_buffers.contains(buffer.buffer)) {
                    m_error = DeviceError::InvalidHandle;
                    return;
                }
            }
            for (const TextureBarrier &barrier : textures) {
                const auto *texture = m_textures.get(barrier.texture);
                if (texture == nullptr) {
                    m_error = DeviceError::InvalidHandle;
                    return;
                }
                const auto after = vulkanTextureSync(barrier.after);
                if (after.layout != VK_IMAGE_LAYOUT_UNDEFINED) {
                    m_texture_references.push_back(barrier.texture);
                    transitionVulkanImage(m_runtime, m_commands, (*texture)->image, (*texture)->range,
                                          vulkanTextureSync(barrier.before), after);
                }
            }
            if (!buffers.empty() || std::any_of(textures.begin(), textures.end(), [](const TextureBarrier &barrier) {
                    return barrier.after == TextureState::Undefined;
                })) {
                VkMemoryBarrier2 memory{};
                memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                memory.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                memory.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                memory.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                memory.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                VkDependencyInfo dependency{};
                dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency.memoryBarrierCount = 1;
                dependency.pMemoryBarriers = &memory;
                m_runtime.vk.vkCmdPipelineBarrier2(m_commands, &dependency);
            }
        } catch (const std::bad_alloc &) {
            m_error = DeviceError::OutOfMemory;
        }
    }

    void beginDebugMarker(std::string_view name) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_closed && m_runtime.inst.vkCmdBeginDebugUtilsLabelEXT) {
            try {
                const std::string label(name);
                m_runtime.beginLabel(m_commands, label.c_str());
                ++m_label_depth;
            } catch (const std::bad_alloc &) {
                m_error = DeviceError::OutOfMemory;
            }
        }
    }

    void endDebugMarker() noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_closed && m_label_depth != 0) {
            m_runtime.endLabel(m_commands);
            --m_label_depth;
        }
    }

    void writeTimestamp(std::uint32_t query_index) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed || !m_queries) {
            return;
        }
        if (query_index >= query_count || m_written[query_index]) {
            m_error = DeviceError::InvalidDescriptor;
            return;
        }
        m_runtime.vk.vkCmdWriteTimestamp2(m_commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, query_index);
        m_written.set(query_index);
    }

    std::expected<double, DeviceError> readTimestampNanoseconds(std::uint32_t query_index) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_queries) {
            return std::unexpected(DeviceError::Unsupported);
        }
        if (!m_completed || query_index >= query_count || !m_written[query_index]) {
            return std::unexpected(DeviceError::InvalidState);
        }
        std::uint64_t ticks = 0;
        const VkResult result = m_runtime.vk.vkGetQueryPoolResults(
            m_runtime.device, m_queries, query_index, 1, sizeof(ticks), &ticks, sizeof(ticks), VK_QUERY_RESULT_64_BIT);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        const auto bits = m_runtime.queueProperties.timestampValidBits;
        if (bits < 64) {
            ticks &= (std::uint64_t{1} << bits) - 1;
        }
        return static_cast<double>(ticks) * m_runtime.properties.limits.timestampPeriod;
    }

    [[nodiscard]] std::expected<void, DeviceError> validate(QueueType queue) const noexcept {
        if (m_closed || m_queue != queue || m_label_depth != 0) {
            return std::unexpected(DeviceError::InvalidState);
        }
        if (m_error) {
            return std::unexpected(*m_error);
        }
        if (std::any_of(m_texture_references.begin(), m_texture_references.end(),
                        [this](TextureHandle texture) { return !m_textures.contains(texture); })) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return {};
    }

    [[nodiscard]] VkResult close() noexcept {
        m_closed = true;
        return m_runtime.vk.vkEndCommandBuffer(m_commands);
    }

    [[nodiscard]] VkCommandBuffer commands() const noexcept { return m_commands; }
    void complete() noexcept { m_completed = true; }

  private:
    static constexpr std::uint32_t query_count = 64;
    const VulkanRuntime &m_runtime;
    BufferPool &m_buffers;
    TexturePool &m_textures;
    std::mutex &m_mutex;
    QueueType m_queue;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    std::bitset<query_count> m_written;
    std::optional<DeviceError> m_error;
    std::vector<TextureHandle> m_texture_references;
    std::uint32_t m_label_depth = 0;
    bool m_closed = false;
    bool m_completed = false;
};

class VulkanDevice final : public Device {
  public:
    [[nodiscard]] std::expected<void, DeviceError> initialize(const DeviceCreateInfo &info) {
        if (auto initialized = m_runtime.initialize(info.enableValidation); !initialized) {
            return initialized;
        }
        m_capabilities.supportsComputeQueue = (m_runtime.queueProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        m_capabilities.supportsCopyQueue = true;
        m_capabilities.supportsDebugMarkers = m_runtime.inst.vkCmdBeginDebugUtilsLabelEXT != nullptr;
        m_capabilities.supportsTimestampQueries = m_runtime.queueProperties.timestampValidBits != 0;
        m_capabilities.supportsValidation = m_runtime.validationEnabled;
        try {
            m_frames.resize(info.framesInFlight);
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        return {};
    }

    [[nodiscard]] const Capabilities &getCapabilities() const noexcept override { return m_capabilities; }

    std::expected<BufferHandle, DeviceError> createBuffer(const BufferDesc &description) override {
        VkBufferUsageFlags usage = 0;
        if ((description.usage & BufferUsage::Vertex) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        }
        if ((description.usage & BufferUsage::Index) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }
        if ((description.usage & BufferUsage::Constant) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        }
        if ((description.usage & BufferUsage::Storage) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }
        if ((description.usage & BufferUsage::Indirect) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        }
        if ((description.usage & BufferUsage::CopySource) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }
        if ((description.usage & BufferUsage::CopyDestination) != BufferUsage::None) {
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }
        if (description.size == 0 || usage == 0) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        try {
            const std::string name(description.debugName);
            auto buffer = std::make_unique<VulkanBuffer>(m_runtime.allocator);
            VkBufferCreateInfo create{};
            create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            create.size = description.size;
            create.usage = usage;
            const auto allocation = vulkanAllocationInfo(description.memory);
            std::scoped_lock lock(m_mutex);
            const VkResult result = vmaCreateBuffer(m_runtime.allocator, &create, &allocation, &buffer->buffer,
                                                    &buffer->allocation, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            m_runtime.nameObject(VK_OBJECT_TYPE_BUFFER, vulkanHandle(buffer->buffer), name.c_str());
            return m_buffers.emplace(std::move(buffer));
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<TextureHandle, DeviceError> createTexture(const TextureDesc &description) override {
        const VkFormat format = vulkanFormat(description.format);
        const VkImageUsageFlags usage = vulkanTextureUsage(description.usage);
        if (description.width == 0 || description.height == 0 || description.depth == 0 || description.mipLevels == 0 ||
            description.arrayLayers == 0 || format == VK_FORMAT_UNDEFINED || usage == 0 ||
            (description.depth > 1 && description.arrayLayers != 1) ||
            description.mipLevels > static_cast<std::uint32_t>(std::bit_width(
                                        std::max({description.width, description.height, description.depth})))) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        if (description.memory != MemoryType::GpuOnly) {
            return std::unexpected(DeviceError::Unsupported);
        }
        try {
            const std::string name(description.debugName);
            auto texture = std::make_unique<VulkanTexture>(m_runtime.allocator);
            texture->range.aspectMask = format == VK_FORMAT_D24_UNORM_S8_UINT
                                            ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                            : VK_IMAGE_ASPECT_COLOR_BIT;
            texture->range.levelCount = description.mipLevels;
            texture->range.layerCount = description.arrayLayers;
            VkImageCreateInfo create{};
            create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            create.imageType = description.depth == 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D;
            create.format = format;
            create.extent = {description.width, description.height, description.depth};
            create.mipLevels = description.mipLevels;
            create.arrayLayers = description.arrayLayers;
            create.samples = VK_SAMPLE_COUNT_1_BIT;
            create.usage = usage;
            VkImageFormatProperties properties{};
            const VkResult supported = m_runtime.inst.vkGetPhysicalDeviceImageFormatProperties(
                m_runtime.physicalDevice, format, create.imageType, create.tiling, usage, 0, &properties);
            if (supported != VK_SUCCESS) {
                return std::unexpected(vulkanError(supported));
            }
            if (description.width > properties.maxExtent.width || description.height > properties.maxExtent.height ||
                description.depth > properties.maxExtent.depth || description.mipLevels > properties.maxMipLevels ||
                description.arrayLayers > properties.maxArrayLayers) {
                return std::unexpected(DeviceError::Unsupported);
            }
            const auto allocation = vulkanAllocationInfo(description.memory);
            std::scoped_lock lock(m_mutex);
            const VkResult result = vmaCreateImage(m_runtime.allocator, &create, &allocation, &texture->image,
                                                   &texture->allocation, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            m_runtime.nameObject(VK_OBJECT_TYPE_IMAGE, vulkanHandle(texture->image), name.c_str());
            return m_textures.emplace(std::move(texture));
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<void, DeviceError> destroy(BufferHandle buffer) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_buffers.markForDestroy(buffer)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return {};
    }

    std::expected<void, DeviceError> destroy(TextureHandle texture) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_textures.markForDestroy(texture)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return {};
    }

    bool isAlive(BufferHandle buffer) const noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_buffers.contains(buffer);
    }

    bool isAlive(TextureHandle texture) const noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_textures.contains(texture);
    }

    std::expected<CommandList *, DeviceError> beginCommandList(QueueType queue) override {
        if (queue != QueueType::Graphics && queue != QueueType::Copy &&
            !(queue == QueueType::Compute && m_capabilities.supportsComputeQueue)) {
            return std::unexpected(DeviceError::Unsupported);
        }
        try {
            std::scoped_lock lock(m_mutex);
            auto commands = std::make_unique<VulkanCommandList>(m_runtime, m_buffers, m_textures, m_mutex, queue);
            if (auto initialized = commands->initialize(); !initialized) {
                return std::unexpected(initialized.error());
            }
            auto *result = commands.get();
            m_frames[m_frame].push_back(std::move(commands));
            return result;
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<SubmitToken, DeviceError> submit(QueueType queue, std::span<CommandList *const> lists,
                                                   std::span<const SyncPoint> waits) override {
        std::scoped_lock lock(m_mutex);
        if (lists.empty() || lists.size() > std::numeric_limits<std::uint32_t>::max() ||
            std::any_of(waits.begin(), waits.end(),
                        [this](SyncPoint wait) { return wait.token.value == 0 || wait.token.value > m_last_token; })) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        try {
            std::vector<VulkanCommandList *> commands;
            std::vector<VkCommandBufferSubmitInfo> buffers;
            commands.reserve(lists.size());
            buffers.reserve(lists.size());
            const auto &frame = m_frames[m_frame];
            for (CommandList *list : lists) {
                const auto found =
                    std::find_if(frame.begin(), frame.end(), [list](const auto &item) { return item.get() == list; });
                if (found == frame.end() || std::find(commands.begin(), commands.end(), list) != commands.end()) {
                    return std::unexpected(DeviceError::InvalidHandle);
                }
                if (const auto valid = (*found)->validate(queue); !valid) {
                    return std::unexpected(valid.error());
                }
                commands.push_back(found->get());
                VkCommandBufferSubmitInfo buffer{};
                buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                buffer.commandBuffer = (*found)->commands();
                buffers.push_back(buffer);
            }
            for (VulkanCommandList *command : commands) {
                if (const VkResult result = command->close(); result != VK_SUCCESS) {
                    return std::unexpected(vulkanError(result));
                }
            }

            const auto &vk = m_runtime.vk;
            VkFence fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fence_create{};
            fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkResult result = vk.vkCreateFence(m_runtime.device, &fence_create, nullptr, &fence);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.commandBufferInfoCount = static_cast<std::uint32_t>(buffers.size());
            submit_info.pCommandBufferInfos = buffers.data();
            result = vk.vkQueueSubmit2(m_runtime.queue, 1, &submit_info, fence);
            if (result == VK_SUCCESS) {
                result = vk.vkWaitForFences(m_runtime.device, 1, &fence, VK_TRUE, UINT64_MAX);
            }
            vk.vkDestroyFence(m_runtime.device, fence, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            for (VulkanCommandList *command : commands) {
                command->complete();
            }
            return SubmitToken{++m_last_token};
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    void beginFrame() noexcept override {
        std::scoped_lock lock(m_mutex);
        m_frame = (m_frame + 1) % m_frames.size();
        m_frames[m_frame].clear();
        m_buffers.reclaimMarked();
        m_textures.reclaimMarked();
    }

    void endFrame() noexcept override {}

  private:
    VulkanRuntime m_runtime;
    mutable std::mutex m_mutex;
    Capabilities m_capabilities;
    BufferPool m_buffers;
    TexturePool m_textures;
    std::vector<std::vector<std::unique_ptr<VulkanCommandList>>> m_frames;
    std::size_t m_frame = 0;
    std::uint64_t m_last_token = 0;
};

} // namespace

std::expected<std::unique_ptr<Device>, DeviceError> createVulkanDevice(const DeviceCreateInfo &create_info) {
    if (create_info.framesInFlight == 0) {
        return std::unexpected(DeviceError::InvalidDescriptor);
    }
    try {
        auto device = std::make_unique<VulkanDevice>();
        if (auto initialized = device->initialize(create_info); !initialized) {
            return std::unexpected(initialized.error());
        }
        return device;
    } catch (const std::bad_alloc &) {
        return std::unexpected(DeviceError::OutOfMemory);
    }
}

} // namespace star::rhi
#else
namespace star::rhi {

std::expected<std::unique_ptr<Device>, DeviceError> createVulkanDevice(const DeviceCreateInfo &create_info) {
    return std::unexpected(create_info.framesInFlight == 0 ? DeviceError::InvalidDescriptor
                                                           : DeviceError::BackendUnavailable);
}

} // namespace star::rhi
#endif
