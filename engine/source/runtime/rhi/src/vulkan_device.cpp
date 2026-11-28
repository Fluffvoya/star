#include "rhi/vulkan.hpp"

#ifdef STAR_WITH_VULKAN
#include "resource_pool.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

struct SDL_Window;

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
    MemoryType memory = MemoryType::GpuOnly;
    std::uint64_t size = 0;
};

struct VulkanTexture {
    VulkanTexture(VmaAllocator owner, const VulkanRuntime &owner_runtime, bool ownsImage = true)
        : allocator(owner), runtime(&owner_runtime), owns_image(ownsImage) {}
    ~VulkanTexture() {
        if (view && runtime != nullptr) {
            runtime->vk.vkDestroyImageView(runtime->device, view, nullptr);
        }
        if (image && owns_image) {
            vmaDestroyImage(allocator, image, allocation);
        }
    }
    VmaAllocator allocator;
    const VulkanRuntime *runtime = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageSubresourceRange range{};
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    bool owns_image = true;
};

struct VulkanShader {
    VkShaderModule module = VK_NULL_HANDLE;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanTextureView {
    VkImageView view = VK_NULL_HANDLE;
};

struct VulkanBindGroupLayout {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<BindingType> types;
};

struct VulkanPipelineLayout {
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct VulkanBindGroup {
    VkDescriptorSet set = VK_NULL_HANDLE;
};

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct VulkanSwapchain {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFence acquire_fence = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    TextureFormat rhi_format = TextureFormat::Bgra8Unorm;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t acquired = std::numeric_limits<std::uint32_t>::max();
    bool vsync = true;
    std::vector<VkImage> images;
    std::vector<TextureHandle> textures;
};

using BufferPool = ResourcePool<std::unique_ptr<VulkanBuffer>, BufferHandle>;
using TexturePool = ResourcePool<std::unique_ptr<VulkanTexture>, TextureHandle>;
using ShaderPool = ResourcePool<VulkanShader, ShaderModuleHandle>;
using SamplerPool = ResourcePool<VulkanSampler, SamplerHandle>;
using ViewPool = ResourcePool<VulkanTextureView, TextureViewHandle>;
using BindGroupLayoutPool = ResourcePool<VulkanBindGroupLayout, BindGroupLayoutHandle>;
using PipelineLayoutPool = ResourcePool<VulkanPipelineLayout, PipelineLayoutHandle>;
using BindGroupPool = ResourcePool<VulkanBindGroup, BindGroupHandle>;
using PipelinePool = ResourcePool<VulkanPipeline, GraphicsPipelineHandle>;
using SwapchainPool = ResourcePool<VulkanSwapchain, SwapchainHandle>;

struct DevicePools {
    BufferPool buffers;
    TexturePool textures;
    ShaderPool shaders;
    SamplerPool samplers;
    ViewPool views;
    BindGroupLayoutPool bind_group_layouts;
    PipelineLayoutPool pipeline_layouts;
    BindGroupPool bind_groups;
    PipelinePool pipelines;
    SwapchainPool swapchains;
};

[[nodiscard]] VkShaderStageFlagBits vulkanShaderStage(ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::Vertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Fragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_VERTEX_BIT;
}

[[nodiscard]] VkFilter vulkanFilter(FilterMode mode) noexcept {
    return mode == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

[[nodiscard]] VkSamplerAddressMode vulkanAddressMode(AddressMode mode) noexcept {
    switch (mode) {
    case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

[[nodiscard]] VkFormat vulkanVertexFormat(VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::Float:
        return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UByte4Norm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::UByte4:
        return VK_FORMAT_R8G8B8A8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkPrimitiveTopology vulkanTopology(PrimitiveTopology topology) noexcept {
    switch (topology) {
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

[[nodiscard]] VkCullModeFlags vulkanCullMode(CullMode mode) noexcept {
    switch (mode) {
    case CullMode::None:
        return VK_CULL_MODE_NONE;
    case CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

[[nodiscard]] VkCompareOp vulkanCompareOp(CompareOp op) noexcept {
    switch (op) {
    case CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}

[[nodiscard]] VkDescriptorType vulkanBindingType(BindingType type) noexcept {
    switch (type) {
    case BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

class VulkanCommandList final : public CommandList {
  public:
    VulkanCommandList(const VulkanRuntime &runtime, DevicePools &pools, std::mutex &mutex, QueueType queue)
        : m_runtime(runtime), m_pools(pools), m_mutex(mutex), m_queue(queue) {}

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
                if (!m_pools.buffers.contains(buffer.buffer)) {
                    m_error = DeviceError::InvalidHandle;
                    return;
                }
            }
            for (const TextureBarrier &barrier : textures) {
                const auto *texture = m_pools.textures.get(barrier.texture);
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

    void beginRenderPass(const RenderPassBeginInfo &description) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed || m_in_pass) {
            m_error = DeviceError::InvalidState;
            return;
        }
        auto *texture = m_pools.textures.get(description.color_target);
        if (texture == nullptr || (*texture)->view == VK_NULL_HANDLE) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        const VulkanTextureSync initial{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
        transitionVulkanImage(m_runtime, m_commands, (*texture)->image, (*texture)->range, initial,
                              vulkanTextureSync(TextureState::ColorAttachment));

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = (*texture)->view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = description.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {description.clear_color[0], description.clear_color[1], description.clear_color[2],
                                  description.clear_color[3]};

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea = {{0, 0}, (*texture)->extent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        m_runtime.vk.vkCmdBeginRendering(m_commands, &rendering);

        VkViewport viewport{
            0.0F, 0.0F, static_cast<float>((*texture)->extent.width), static_cast<float>((*texture)->extent.height),
            0.0F, 1.0F};
        m_runtime.vk.vkCmdSetViewport(m_commands, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, (*texture)->extent};
        m_runtime.vk.vkCmdSetScissor(m_commands, 0, 1, &scissor);

        m_texture_references.push_back(description.color_target);
        m_pass_target = description.color_target;
        m_pass_present = description.present_after;
        m_in_pass = true;
    }

    void endRenderPass() noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed || !m_in_pass) {
            m_error = DeviceError::InvalidState;
            return;
        }
        m_runtime.vk.vkCmdEndRendering(m_commands);
        m_in_pass = false;
        auto *texture = m_pools.textures.get(m_pass_target);
        if (texture == nullptr) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        const VulkanTextureSync after = m_pass_present ? VulkanTextureSync{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}
                                                       : vulkanTextureSync(TextureState::Sampled);
        transitionVulkanImage(m_runtime, m_commands, (*texture)->image, (*texture)->range,
                              vulkanTextureSync(TextureState::ColorAttachment), after);
    }

    void setViewport(float x, float y, float width, float height) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        VkViewport viewport{x, y, width, height, 0.0F, 1.0F};
        m_runtime.vk.vkCmdSetViewport(m_commands, 0, 1, &viewport);
    }

    void setScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        VkRect2D scissor{{x, y}, {width, height}};
        m_runtime.vk.vkCmdSetScissor(m_commands, 0, 1, &scissor);
    }

    void bindGraphicsPipeline(GraphicsPipelineHandle pipeline,
                              std::span<const BindGroupHandle> bind_groups) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        const VulkanPipeline *resolved = m_pools.pipelines.get(pipeline);
        if (resolved == nullptr) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        m_runtime.vk.vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, resolved->pipeline);
        m_current_layout = resolved->layout;
        if (bind_groups.empty()) {
            return;
        }
        try {
            std::vector<VkDescriptorSet> sets;
            sets.reserve(bind_groups.size());
            for (const BindGroupHandle group : bind_groups) {
                const VulkanBindGroup *resolved_group = m_pools.bind_groups.get(group);
                if (resolved_group == nullptr) {
                    m_error = DeviceError::InvalidHandle;
                    return;
                }
                sets.push_back(resolved_group->set);
            }
            m_runtime.vk.vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_current_layout, 0,
                                                 static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
        } catch (const std::bad_alloc &) {
            m_error = DeviceError::OutOfMemory;
        }
    }

    void pushConstants(const void *data, std::uint32_t size) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        if (m_current_layout == VK_NULL_HANDLE || data == nullptr || size == 0) {
            m_error = DeviceError::InvalidState;
            return;
        }
        m_runtime.vk.vkCmdPushConstants(m_commands, m_current_layout,
                                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
    }

    void bindVertexBuffer(BufferHandle buffer, std::uint64_t offset) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        const auto *resolved = m_pools.buffers.get(buffer);
        if (resolved == nullptr) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        const VkBuffer handle = (*resolved)->buffer;
        m_runtime.vk.vkCmdBindVertexBuffers(m_commands, 0, 1, &handle, &offset);
    }

    void bindIndexBuffer(BufferHandle buffer, std::uint64_t offset, bool index32) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        const auto *resolved = m_pools.buffers.get(buffer);
        if (resolved == nullptr) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        m_runtime.vk.vkCmdBindIndexBuffer(m_commands, (*resolved)->buffer, offset,
                                          index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    }

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count, std::uint32_t first_vertex,
              std::uint32_t first_instance) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_closed && m_in_pass) {
            m_runtime.vk.vkCmdDraw(m_commands, vertex_count, instance_count, first_vertex, first_instance);
        }
    }

    void drawIndexed(std::uint32_t index_count, std::uint32_t instance_count, std::uint32_t first_index,
                     std::int32_t vertex_offset, std::uint32_t first_instance) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_closed && m_in_pass) {
            m_runtime.vk.vkCmdDrawIndexed(m_commands, index_count, instance_count, first_index, vertex_offset,
                                          first_instance);
        }
    }

    void copyBufferToTexture(BufferHandle source, TextureHandle destination) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        const auto *buffer = m_pools.buffers.get(source);
        const auto *texture = m_pools.textures.get(destination);
        if (buffer == nullptr || texture == nullptr) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        const VulkanTextureSync before{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
        transitionVulkanImage(m_runtime, m_commands, (*texture)->image, (*texture)->range, before,
                              vulkanTextureSync(TextureState::CopyDestination));
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {(*texture)->extent.width, (*texture)->extent.height, 1};
        m_runtime.vk.vkCmdCopyBufferToImage(m_commands, (*buffer)->buffer, (*texture)->image,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transitionVulkanImage(m_runtime, m_commands, (*texture)->image, (*texture)->range,
                              vulkanTextureSync(TextureState::CopyDestination),
                              vulkanTextureSync(TextureState::Sampled));
        m_texture_references.push_back(destination);
    }

    void copyTextureToBuffer(TextureHandle source, BufferHandle destination) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_closed) {
            return;
        }
        const auto *texture = m_pools.textures.get(source);
        const auto *buffer = m_pools.buffers.get(destination);
        if (texture == nullptr || buffer == nullptr) {
            m_error = DeviceError::InvalidHandle;
            return;
        }
        transitionVulkanImage(m_runtime, m_commands, (*texture)->image, (*texture)->range,
                              vulkanTextureSync(TextureState::Sampled), vulkanTextureSync(TextureState::CopySource));
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {(*texture)->extent.width, (*texture)->extent.height, 1};
        m_runtime.vk.vkCmdCopyImageToBuffer(m_commands, (*texture)->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            (*buffer)->buffer, 1, &region);
        m_texture_references.push_back(source);
    }

    [[nodiscard]] std::expected<void, DeviceError> validate(QueueType queue) const noexcept {
        if (m_closed || m_queue != queue || m_label_depth != 0 || m_in_pass) {
            return std::unexpected(DeviceError::InvalidState);
        }
        if (m_error) {
            return std::unexpected(*m_error);
        }
        if (std::any_of(m_texture_references.begin(), m_texture_references.end(),
                        [this](TextureHandle texture) { return !m_pools.textures.contains(texture); })) {
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
    DevicePools &m_pools;
    std::mutex &m_mutex;
    QueueType m_queue;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    std::bitset<query_count> m_written;
    std::optional<DeviceError> m_error;
    std::vector<TextureHandle> m_texture_references;
    TextureHandle m_pass_target;
    VkPipelineLayout m_current_layout = VK_NULL_HANDLE;
    std::uint32_t m_label_depth = 0;
    bool m_in_pass = false;
    bool m_pass_present = false;
    bool m_closed = false;
    bool m_completed = false;
};

class VulkanDevice final : public Device {
  public:
    [[nodiscard]] std::expected<void, DeviceError> initialize(const DeviceCreateInfo &info) {
        if (auto initialized =
                m_runtime.initialize(info.enableValidation, static_cast<SDL_Window *>(info.native_window));
            !initialized) {
            return initialized;
        }
        m_capabilities.supportsComputeQueue = (m_runtime.queueProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        m_capabilities.supportsCopyQueue = true;
        m_capabilities.supportsDebugMarkers = m_runtime.inst.vkCmdBeginDebugUtilsLabelEXT != nullptr;
        m_capabilities.supportsTimestampQueries = m_runtime.queueProperties.timestampValidBits != 0;
        m_capabilities.supportsValidation = m_runtime.validationEnabled;
        m_capabilities.supportsPresentation = m_runtime.surface != VK_NULL_HANDLE;
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
            buffer->memory = description.memory;
            buffer->size = description.size;
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
            return m_pools.buffers.emplace(std::move(buffer));
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
            auto texture = std::make_unique<VulkanTexture>(m_runtime.allocator, m_runtime);
            texture->range.aspectMask = format == VK_FORMAT_D24_UNORM_S8_UINT
                                            ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                            : VK_IMAGE_ASPECT_COLOR_BIT;
            texture->range.levelCount = description.mipLevels;
            texture->range.layerCount = description.arrayLayers;
            texture->format = format;
            texture->extent = {description.width, description.height};
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
            VkImageViewCreateInfo view_create{};
            view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_create.image = texture->image;
            view_create.viewType = description.depth == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_3D;
            view_create.format = format;
            view_create.subresourceRange = texture->range;
            const VkResult view_result =
                m_runtime.vk.vkCreateImageView(m_runtime.device, &view_create, nullptr, &texture->view);
            if (view_result != VK_SUCCESS) {
                return std::unexpected(vulkanError(view_result));
            }
            return m_pools.textures.emplace(std::move(texture));
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<void, DeviceError> readBuffer(BufferHandle buffer, std::span<std::byte> data,
                                                std::uint64_t offset) override {
        std::scoped_lock lock(m_mutex);
        auto *entry = m_pools.buffers.get(buffer);
        if (entry == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        VulkanBuffer &source = **entry;
        if (source.memory != MemoryType::Readback) {
            return std::unexpected(DeviceError::Unsupported);
        }
        if (offset > source.size || data.size() > source.size - offset) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        if (data.empty()) {
            return {};
        }
        void *mapped = nullptr;
        if (vmaMapMemory(m_runtime.allocator, source.allocation, &mapped) != VK_SUCCESS || mapped == nullptr) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        std::memcpy(data.data(), static_cast<const std::byte *>(mapped) + offset, data.size());
        vmaUnmapMemory(m_runtime.allocator, source.allocation);
        return {};
    }

    std::expected<void, DeviceError> writeBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                                 std::uint64_t offset) override {
        std::scoped_lock lock(m_mutex);
        auto *entry = m_pools.buffers.get(buffer);
        if (entry == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        VulkanBuffer &target = **entry;
        if (target.memory != MemoryType::Upload) {
            return std::unexpected(DeviceError::Unsupported);
        }
        if (offset > target.size || data.size() > target.size - offset) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        if (data.empty()) {
            return {};
        }
        void *mapped = nullptr;
        if (vmaMapMemory(m_runtime.allocator, target.allocation, &mapped) != VK_SUCCESS || mapped == nullptr) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
        std::memcpy(static_cast<std::byte *>(mapped) + offset, data.data(), data.size());
        vmaUnmapMemory(m_runtime.allocator, target.allocation);
        return {};
    }

    std::expected<ShaderModuleHandle, DeviceError> createShaderModule(const ShaderModuleDesc &description) override {
        if (description.spirv.empty() || description.spirv.size() > std::numeric_limits<std::size_t>::max() / 4) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        VkShaderModuleCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create.codeSize = description.spirv.size() * sizeof(std::uint32_t);
        create.pCode = description.spirv.data();
        std::scoped_lock lock(m_mutex);
        VulkanShader shader;
        const VkResult result = m_runtime.vk.vkCreateShaderModule(m_runtime.device, &create, nullptr, &shader.module);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        return m_pools.shaders.emplace(shader);
    }

    std::expected<SamplerHandle, DeviceError> createSampler(const SamplerDesc &description) override {
        VkSamplerCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        create.magFilter = vulkanFilter(description.mag_filter);
        create.minFilter = vulkanFilter(description.min_filter);
        create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        create.addressModeU = vulkanAddressMode(description.address_u);
        create.addressModeV = vulkanAddressMode(description.address_v);
        create.addressModeW = vulkanAddressMode(description.address_w);
        create.maxLod = VK_LOD_CLAMP_NONE;
        std::scoped_lock lock(m_mutex);
        VulkanSampler sampler;
        const VkResult result = m_runtime.vk.vkCreateSampler(m_runtime.device, &create, nullptr, &sampler.sampler);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        return m_pools.samplers.emplace(sampler);
    }

    std::expected<TextureViewHandle, DeviceError> createTextureView(const TextureViewDesc &description) override {
        std::scoped_lock lock(m_mutex);
        auto *entry = m_pools.textures.get(description.texture);
        if (entry == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        VkImageViewCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create.image = (*entry)->image;
        create.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create.format = (*entry)->format;
        create.subresourceRange = (*entry)->range;
        VulkanTextureView view;
        const VkResult result = m_runtime.vk.vkCreateImageView(m_runtime.device, &create, nullptr, &view.view);
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        return m_pools.views.emplace(view);
    }

    std::expected<BindGroupLayoutHandle, DeviceError>
    createBindGroupLayout(const BindGroupLayoutDesc &description) override {
        if (description.entries.empty()) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        try {
            VulkanBindGroupLayout layout;
            layout.bindings.reserve(description.entries.size());
            layout.types.reserve(description.entries.size());
            for (const BindGroupLayoutEntry &entry : description.entries) {
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = entry.binding;
                binding.descriptorType = vulkanBindingType(entry.type);
                binding.descriptorCount = 1;
                binding.stageFlags = vulkanShaderStage(entry.visibility);
                layout.bindings.push_back(binding);
                layout.types.push_back(entry.type);
            }
            VkDescriptorSetLayoutCreateInfo create{};
            create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            create.bindingCount = static_cast<std::uint32_t>(layout.bindings.size());
            create.pBindings = layout.bindings.data();
            std::scoped_lock lock(m_mutex);
            const VkResult result =
                m_runtime.vk.vkCreateDescriptorSetLayout(m_runtime.device, &create, nullptr, &layout.layout);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            return m_pools.bind_group_layouts.emplace(std::move(layout));
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<PipelineLayoutHandle, DeviceError>
    createPipelineLayout(const PipelineLayoutDesc &description) override {
        try {
            std::vector<VkDescriptorSetLayout> layouts;
            layouts.reserve(description.bind_groups.size());
            std::scoped_lock lock(m_mutex);
            for (const BindGroupLayoutHandle handle : description.bind_groups) {
                const VulkanBindGroupLayout *layout = m_pools.bind_group_layouts.get(handle);
                if (layout == nullptr) {
                    return std::unexpected(DeviceError::InvalidHandle);
                }
                layouts.push_back(layout->layout);
            }
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            range.offset = 0;
            range.size = description.push_constant_size;
            VkPipelineLayoutCreateInfo create{};
            create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            create.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
            create.pSetLayouts = layouts.data();
            if (description.push_constant_size != 0) {
                create.pushConstantRangeCount = 1;
                create.pPushConstantRanges = &range;
            }
            VulkanPipelineLayout result_layout;
            const VkResult result =
                m_runtime.vk.vkCreatePipelineLayout(m_runtime.device, &create, nullptr, &result_layout.layout);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            return m_pools.pipeline_layouts.emplace(result_layout);
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<BindGroupHandle, DeviceError> createBindGroup(const BindGroupDesc &description) override {
        try {
            std::scoped_lock lock(m_mutex);
            const VulkanBindGroupLayout *layout = m_pools.bind_group_layouts.get(description.layout);
            if (layout == nullptr) {
                return std::unexpected(DeviceError::InvalidHandle);
            }
            const VkDescriptorPool pool = ensureDescriptorPool();
            if (pool == VK_NULL_HANDLE) {
                return std::unexpected(DeviceError::OutOfMemory);
            }
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &layout->layout;
            VulkanBindGroup group;
            VkResult result = m_runtime.vk.vkAllocateDescriptorSets(m_runtime.device, &allocate, &group.set);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            std::vector<VkDescriptorImageInfo> images;
            std::vector<VkDescriptorBufferInfo> buffers;
            std::vector<VkWriteDescriptorSet> writes;
            images.reserve(layout->bindings.size());
            buffers.reserve(layout->bindings.size());
            writes.reserve(layout->bindings.size());
            for (std::size_t index = 0; index < layout->bindings.size(); ++index) {
                const VkDescriptorSetLayoutBinding &binding = layout->bindings[index];
                const auto found = std::find_if(
                    description.entries.begin(), description.entries.end(),
                    [&binding](const BindGroupEntry &candidate) { return candidate.binding == binding.binding; });
                if (found == description.entries.end()) {
                    return std::unexpected(DeviceError::InvalidDescriptor);
                }
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = group.set;
                write.dstBinding = binding.binding;
                write.descriptorCount = 1;
                write.descriptorType = binding.descriptorType;
                if (layout->types[index] == BindingType::CombinedImageSampler) {
                    const VulkanTextureView *view = m_pools.views.get(found->texture_view);
                    const VulkanSampler *sampler = m_pools.samplers.get(found->sampler);
                    if (view == nullptr || sampler == nullptr) {
                        return std::unexpected(DeviceError::InvalidHandle);
                    }
                    VkDescriptorImageInfo info{};
                    info.sampler = sampler->sampler;
                    info.imageView = view->view;
                    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    images.push_back(info);
                    write.pImageInfo = &images.back();
                } else {
                    auto *entry = m_pools.buffers.get(found->buffer);
                    if (entry == nullptr) {
                        return std::unexpected(DeviceError::InvalidHandle);
                    }
                    VkDescriptorBufferInfo info{};
                    info.buffer = (*entry)->buffer;
                    info.offset = found->offset;
                    info.range = found->size == 0 ? VK_WHOLE_SIZE : found->size;
                    buffers.push_back(info);
                    write.pBufferInfo = &buffers.back();
                }
                writes.push_back(write);
            }
            m_runtime.vk.vkUpdateDescriptorSets(m_runtime.device, static_cast<std::uint32_t>(writes.size()),
                                                writes.data(), 0, nullptr);
            return m_pools.bind_groups.emplace(group);
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<GraphicsPipelineHandle, DeviceError>
    createGraphicsPipeline(const GraphicsPipelineDesc &description) override {
        try {
            std::scoped_lock lock(m_mutex);
            const VulkanPipelineLayout *layout = m_pools.pipeline_layouts.get(description.layout);
            const VulkanShader *vertex = m_pools.shaders.get(description.vertex_shader);
            const VulkanShader *fragment = m_pools.shaders.get(description.fragment_shader);
            if (layout == nullptr || vertex == nullptr || fragment == nullptr) {
                return std::unexpected(DeviceError::InvalidHandle);
            }
            if (description.color_formats.empty()) {
                return std::unexpected(DeviceError::InvalidDescriptor);
            }

            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertex->module;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragment->module;
            stages[1].pName = "main";

            std::vector<VkVertexInputBindingDescription> bindings;
            std::vector<VkVertexInputAttributeDescription> attributes;
            bindings.reserve(description.vertex_buffers.size());
            for (const VertexBufferLayout &buffer : description.vertex_buffers) {
                VkVertexInputBindingDescription binding{};
                binding.binding = static_cast<std::uint32_t>(bindings.size());
                binding.stride = buffer.stride;
                binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                bindings.push_back(binding);
                for (const VertexAttribute &attribute : buffer.attributes) {
                    VkVertexInputAttributeDescription entry{};
                    entry.location = attribute.location;
                    entry.binding = binding.binding;
                    entry.format = vulkanVertexFormat(attribute.format);
                    entry.offset = attribute.offset;
                    attributes.push_back(entry);
                }
            }

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
            vertex_input.pVertexBindingDescriptions = bindings.data();
            vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
            vertex_input.pVertexAttributeDescriptions = attributes.data();

            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = vulkanTopology(description.topology);

            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = vulkanCullMode(description.cull);
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0F;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth{};
            depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth.depthTestEnable = description.has_depth && description.depth_test ? VK_TRUE : VK_FALSE;
            depth.depthWriteEnable = description.has_depth && description.depth_write ? VK_TRUE : VK_FALSE;
            depth.depthCompareOp = vulkanCompareOp(description.depth_compare);

            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                        VK_COLOR_COMPONENT_A_BIT;
            attachment.blendEnable = description.blend == BlendMode::Alpha ? VK_TRUE : VK_FALSE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            std::vector<VkPipelineColorBlendAttachmentState> attachments(description.color_formats.size(), attachment);

            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            blend.pAttachments = attachments.data();

            constexpr std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
            dynamic.pDynamicStates = dynamic_states.data();

            std::vector<VkFormat> color_formats;
            color_formats.reserve(description.color_formats.size());
            for (const TextureFormat format : description.color_formats) {
                color_formats.push_back(vulkanFormat(format));
            }
            VkPipelineRenderingCreateInfo rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering.colorAttachmentCount = static_cast<std::uint32_t>(color_formats.size());
            rendering.pColorAttachmentFormats = color_formats.data();
            rendering.depthAttachmentFormat =
                description.has_depth ? vulkanFormat(description.depth_format) : VK_FORMAT_UNDEFINED;

            VkGraphicsPipelineCreateInfo create{};
            create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            create.pNext = &rendering;
            create.stageCount = static_cast<std::uint32_t>(stages.size());
            create.pStages = stages.data();
            create.pVertexInputState = &vertex_input;
            create.pInputAssemblyState = &assembly;
            create.pViewportState = &viewport;
            create.pRasterizationState = &raster;
            create.pMultisampleState = &multisample;
            create.pDepthStencilState = &depth;
            create.pColorBlendState = &blend;
            create.pDynamicState = &dynamic;
            create.layout = layout->layout;
            VulkanPipeline pipeline;
            const VkResult result = m_runtime.vk.vkCreateGraphicsPipelines(m_runtime.device, VK_NULL_HANDLE, 1, &create,
                                                                           nullptr, &pipeline.pipeline);
            if (result != VK_SUCCESS) {
                return std::unexpected(vulkanError(result));
            }
            pipeline.layout = layout->layout;
            return m_pools.pipelines.emplace(pipeline);
        } catch (const std::bad_alloc &) {
            return std::unexpected(DeviceError::OutOfMemory);
        }
    }

    std::expected<void, DeviceError> destroy(BufferHandle buffer) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_pools.buffers.markForDestroy(buffer)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return {};
    }

    std::expected<void, DeviceError> destroy(TextureHandle texture) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (!m_pools.textures.markForDestroy(texture)) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return {};
    }

    std::expected<void, DeviceError> destroy(ShaderModuleHandle shader) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanShader *resolved = m_pools.shaders.get(shader);
        if (resolved == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (resolved->module != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroyShaderModule(m_runtime.device, resolved->module, nullptr);
        }
        m_pools.shaders.erase(shader);
        return {};
    }

    std::expected<void, DeviceError> destroy(SamplerHandle sampler) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanSampler *resolved = m_pools.samplers.get(sampler);
        if (resolved == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (resolved->sampler != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroySampler(m_runtime.device, resolved->sampler, nullptr);
        }
        m_pools.samplers.erase(sampler);
        return {};
    }

    std::expected<void, DeviceError> destroy(TextureViewHandle view) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanTextureView *resolved = m_pools.views.get(view);
        if (resolved == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (resolved->view != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroyImageView(m_runtime.device, resolved->view, nullptr);
        }
        m_pools.views.erase(view);
        return {};
    }

    std::expected<void, DeviceError> destroy(BindGroupLayoutHandle layout) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanBindGroupLayout *resolved = m_pools.bind_group_layouts.get(layout);
        if (resolved == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (resolved->layout != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroyDescriptorSetLayout(m_runtime.device, resolved->layout, nullptr);
        }
        m_pools.bind_group_layouts.erase(layout);
        return {};
    }

    std::expected<void, DeviceError> destroy(PipelineLayoutHandle layout) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanPipelineLayout *resolved = m_pools.pipeline_layouts.get(layout);
        if (resolved == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (resolved->layout != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroyPipelineLayout(m_runtime.device, resolved->layout, nullptr);
        }
        m_pools.pipeline_layouts.erase(layout);
        return {};
    }

    std::expected<void, DeviceError> destroy(BindGroupHandle group) noexcept override {
        std::scoped_lock lock(m_mutex);
        if (m_pools.bind_groups.get(group) == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        m_pools.bind_groups.erase(group);
        return {};
    }

    std::expected<void, DeviceError> destroy(GraphicsPipelineHandle pipeline) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanPipeline *resolved = m_pools.pipelines.get(pipeline);
        if (resolved == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        if (resolved->pipeline != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroyPipeline(m_runtime.device, resolved->pipeline, nullptr);
        }
        m_pools.pipelines.erase(pipeline);
        return {};
    }

    std::expected<void, DeviceError> destroy(SwapchainHandle handle) noexcept override {
        std::scoped_lock lock(m_mutex);
        VulkanSwapchain *swapchain = m_pools.swapchains.get(handle);
        if (swapchain == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        m_runtime.vk.vkDeviceWaitIdle(m_runtime.device);
        releaseSwapchainResources(*swapchain);
        m_pools.swapchains.erase(handle);
        return {};
    }

    std::expected<SwapchainHandle, DeviceError> createSwapchain(const SwapchainDesc &description) override {
        if (!m_capabilities.supportsPresentation) {
            return std::unexpected(DeviceError::Unsupported);
        }
        if (description.width == 0 || description.height == 0) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        VulkanSwapchain swapchain;
        swapchain.vsync = description.vsync;
        if (auto created = createSwapchainResources(swapchain, description.width, description.height); !created) {
            return std::unexpected(created.error());
        }
        return m_pools.swapchains.emplace(std::move(swapchain));
    }

    std::expected<SwapchainInfo, DeviceError> getSwapchainInfo(SwapchainHandle handle) const override {
        std::scoped_lock lock(m_mutex);
        const VulkanSwapchain *swapchain = m_pools.swapchains.get(handle);
        if (swapchain == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        return SwapchainInfo{swapchain->width, swapchain->height, swapchain->rhi_format,
                             static_cast<std::uint32_t>(swapchain->textures.size())};
    }

    std::expected<TextureHandle, DeviceError> acquireNextImage(SwapchainHandle handle) override {
        std::scoped_lock lock(m_mutex);
        VulkanSwapchain *swapchain = m_pools.swapchains.get(handle);
        if (swapchain == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        std::uint32_t index = 0;
        const VkResult result = m_runtime.vk.vkAcquireNextImageKHR(m_runtime.device, swapchain->swapchain, UINT64_MAX,
                                                                   VK_NULL_HANDLE, swapchain->acquire_fence, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return std::unexpected(DeviceError::Outdated);
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected(vulkanError(result));
        }
        m_runtime.vk.vkWaitForFences(m_runtime.device, 1, &swapchain->acquire_fence, VK_TRUE, UINT64_MAX);
        m_runtime.vk.vkResetFences(m_runtime.device, 1, &swapchain->acquire_fence);
        if (index >= swapchain->textures.size()) {
            return std::unexpected(DeviceError::BackendFailure);
        }
        swapchain->acquired = index;
        return swapchain->textures[index];
    }

    std::expected<void, DeviceError> resizeSwapchain(SwapchainHandle handle, std::uint32_t width,
                                                     std::uint32_t height) override {
        if (width == 0 || height == 0) {
            return std::unexpected(DeviceError::InvalidDescriptor);
        }
        std::scoped_lock lock(m_mutex);
        VulkanSwapchain *swapchain = m_pools.swapchains.get(handle);
        if (swapchain == nullptr) {
            return std::unexpected(DeviceError::InvalidHandle);
        }
        m_runtime.vk.vkDeviceWaitIdle(m_runtime.device);
        releaseSwapchainResources(*swapchain);
        return createSwapchainResources(*swapchain, width, height);
    }

    std::expected<void, DeviceError> present(SwapchainHandle handle) override {
        std::scoped_lock lock(m_mutex);
        VulkanSwapchain *swapchain = m_pools.swapchains.get(handle);
        if (swapchain == nullptr || swapchain->acquired == std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(DeviceError::InvalidState);
        }
        VkPresentInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.swapchainCount = 1;
        info.pSwapchains = &swapchain->swapchain;
        info.pImageIndices = &swapchain->acquired;
        const VkResult result = m_runtime.vk.vkQueuePresentKHR(m_runtime.queue, &info);
        swapchain->acquired = std::numeric_limits<std::uint32_t>::max();
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            return std::unexpected(DeviceError::Outdated);
        }
        if (result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        return {};
    }

    std::expected<void, DeviceError> waitIdle() noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_runtime.vk.vkDeviceWaitIdle(m_runtime.device) == VK_SUCCESS
                   ? std::expected<void, DeviceError>{}
                   : std::unexpected(DeviceError::BackendFailure);
    }

    bool isAlive(BufferHandle buffer) const noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_pools.buffers.contains(buffer);
    }

    bool isAlive(TextureHandle texture) const noexcept override {
        std::scoped_lock lock(m_mutex);
        return m_pools.textures.contains(texture);
    }

    std::expected<CommandList *, DeviceError> beginCommandList(QueueType queue) override {
        if (queue != QueueType::Graphics && queue != QueueType::Copy &&
            !(queue == QueueType::Compute && m_capabilities.supportsComputeQueue)) {
            return std::unexpected(DeviceError::Unsupported);
        }
        try {
            std::scoped_lock lock(m_mutex);
            auto commands = std::make_unique<VulkanCommandList>(m_runtime, m_pools, m_mutex, queue);
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
        m_pools.buffers.reclaimMarked();
        m_pools.textures.reclaimMarked();
    }

    void endFrame() noexcept override {}

  private:
    // Callers hold m_mutex.
    VkDescriptorPool ensureDescriptorPool() {
        if (m_descriptor_pool != VK_NULL_HANDLE) {
            return m_descriptor_pool;
        }
        const std::array<VkDescriptorPoolSize, 3> sizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64},
        }};
        VkDescriptorPoolCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        create.maxSets = 256;
        create.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        create.pPoolSizes = sizes.data();
        if (m_runtime.vk.vkCreateDescriptorPool(m_runtime.device, &create, nullptr, &m_descriptor_pool) != VK_SUCCESS) {
            m_descriptor_pool = VK_NULL_HANDLE;
        }
        return m_descriptor_pool;
    }

    void releaseSwapchainResources(VulkanSwapchain &swapchain) noexcept {
        for (const TextureHandle texture : swapchain.textures) {
            m_pools.textures.erase(texture);
        }
        swapchain.textures.clear();
        swapchain.images.clear();
        if (swapchain.swapchain != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroySwapchainKHR(m_runtime.device, swapchain.swapchain, nullptr);
            swapchain.swapchain = VK_NULL_HANDLE;
        }
        if (swapchain.acquire_fence != VK_NULL_HANDLE) {
            m_runtime.vk.vkDestroyFence(m_runtime.device, swapchain.acquire_fence, nullptr);
            swapchain.acquire_fence = VK_NULL_HANDLE;
        }
        swapchain.acquired = std::numeric_limits<std::uint32_t>::max();
    }

    std::expected<void, DeviceError> createSwapchainResources(VulkanSwapchain &swapchain, std::uint32_t width,
                                                              std::uint32_t height) {
        const auto &inst = m_runtime.inst;
        const auto &vk = m_runtime.vk;
        VkSurfaceCapabilitiesKHR capabilities{};
        if (inst.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_runtime.physicalDevice, m_runtime.surface,
                                                           &capabilities) != VK_SUCCESS) {
            return std::unexpected(DeviceError::BackendUnavailable);
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            return std::unexpected(DeviceError::Unsupported);
        }

        std::uint32_t format_count = 0;
        if (inst.vkGetPhysicalDeviceSurfaceFormatsKHR(m_runtime.physicalDevice, m_runtime.surface, &format_count,
                                                      nullptr) != VK_SUCCESS ||
            format_count == 0) {
            return std::unexpected(DeviceError::BackendUnavailable);
        }
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        if (inst.vkGetPhysicalDeviceSurfaceFormatsKHR(m_runtime.physicalDevice, m_runtime.surface, &format_count,
                                                      formats.data()) != VK_SUCCESS) {
            return std::unexpected(DeviceError::BackendFailure);
        }
        const auto preferred = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR &format) {
            return format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM;
        });
        if (preferred == formats.end()) {
            return std::unexpected(DeviceError::Unsupported);
        }
        const VkSurfaceFormatKHR surface_format = *preferred;

        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!swapchain.vsync) {
            std::uint32_t mode_count = 0;
            if (inst.vkGetPhysicalDeviceSurfacePresentModesKHR(m_runtime.physicalDevice, m_runtime.surface, &mode_count,
                                                               nullptr) == VK_SUCCESS &&
                mode_count != 0) {
                std::vector<VkPresentModeKHR> modes(mode_count);
                if (inst.vkGetPhysicalDeviceSurfacePresentModesKHR(m_runtime.physicalDevice, m_runtime.surface,
                                                                   &mode_count, modes.data()) == VK_SUCCESS) {
                    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
                        present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                    } else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
                        present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                    }
                }
            }
        }

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        std::uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount != 0 && image_count > capabilities.maxImageCount) {
            image_count = capabilities.maxImageCount;
        }

        VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0) {
            for (const auto candidate :
                 {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                  VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
                if ((capabilities.supportedCompositeAlpha & candidate) != 0) {
                    composite = candidate;
                    break;
                }
            }
        }

        VkSwapchainCreateInfoKHR create{};
        create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create.surface = m_runtime.surface;
        create.minImageCount = image_count;
        create.imageFormat = surface_format.format;
        create.imageColorSpace = surface_format.colorSpace;
        create.imageExtent = extent;
        create.imageArrayLayers = 1;
        create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.preTransform = capabilities.currentTransform;
        create.compositeAlpha = composite;
        create.presentMode = present_mode;
        create.clipped = VK_TRUE;
        if (const VkResult result = vk.vkCreateSwapchainKHR(m_runtime.device, &create, nullptr, &swapchain.swapchain);
            result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }

        std::uint32_t actual_count = 0;
        if (vk.vkGetSwapchainImagesKHR(m_runtime.device, swapchain.swapchain, &actual_count, nullptr) != VK_SUCCESS ||
            actual_count == 0) {
            releaseSwapchainResources(swapchain);
            return std::unexpected(DeviceError::BackendFailure);
        }
        swapchain.images.resize(actual_count);
        if (vk.vkGetSwapchainImagesKHR(m_runtime.device, swapchain.swapchain, &actual_count, swapchain.images.data()) !=
            VK_SUCCESS) {
            releaseSwapchainResources(swapchain);
            return std::unexpected(DeviceError::BackendFailure);
        }
        swapchain.images.resize(actual_count);

        std::vector<std::unique_ptr<VulkanTexture>> textures;
        try {
            textures.reserve(actual_count);
            swapchain.textures.reserve(actual_count);
        } catch (const std::bad_alloc &) {
            releaseSwapchainResources(swapchain);
            return std::unexpected(DeviceError::OutOfMemory);
        }
        for (std::uint32_t index = 0; index < actual_count; ++index) {
            auto texture = std::make_unique<VulkanTexture>(m_runtime.allocator, m_runtime, false);
            texture->image = swapchain.images[index];
            texture->format = surface_format.format;
            texture->extent = extent;
            texture->range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            texture->range.levelCount = 1;
            texture->range.layerCount = 1;
            VkImageViewCreateInfo view_create{};
            view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_create.image = texture->image;
            view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_create.format = surface_format.format;
            view_create.subresourceRange = texture->range;
            const VkResult view_result = vk.vkCreateImageView(m_runtime.device, &view_create, nullptr, &texture->view);
            if (view_result != VK_SUCCESS) {
                releaseSwapchainResources(swapchain);
                return std::unexpected(vulkanError(view_result));
            }
            textures.push_back(std::move(texture));
        }

        if (swapchain.acquire_fence == VK_NULL_HANDLE) {
            VkFenceCreateInfo fence_create{};
            fence_create.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vk.vkCreateFence(m_runtime.device, &fence_create, nullptr, &swapchain.acquire_fence) != VK_SUCCESS) {
                releaseSwapchainResources(swapchain);
                return std::unexpected(DeviceError::BackendFailure);
            }
        }

        for (auto &texture : textures) {
            auto handle = m_pools.textures.emplace(std::move(texture));
            if (!handle) {
                releaseSwapchainResources(swapchain);
                return std::unexpected(handle.error());
            }
            swapchain.textures.push_back(*handle);
        }

        swapchain.format = surface_format.format;
        swapchain.rhi_format =
            surface_format.format == VK_FORMAT_B8G8R8A8_UNORM ? TextureFormat::Bgra8Unorm : TextureFormat::Rgba8Unorm;
        swapchain.width = extent.width;
        swapchain.height = extent.height;
        swapchain.acquired = std::numeric_limits<std::uint32_t>::max();
        return {};
    }

    VulkanRuntime m_runtime;
    mutable std::mutex m_mutex;
    Capabilities m_capabilities;
    DevicePools m_pools;
    VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
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
