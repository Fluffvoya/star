#include "rhi/render_graph.hpp"
#include "vulkan_runtime.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using star::rhi::TextureState;
using star::rhi::transitionVulkanImage;
using star::rhi::vulkanHandle;
using star::rhi::VulkanRuntime;
using star::rhi::vulkanTextureSync;

template <typename T> T vkInfo(VkStructureType type) {
    T info{};
    info.sType = type;
    return info;
}

void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " returned " + std::to_string(result));
    }
}

struct Buffer {
    Buffer() = default;
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    ~Buffer() {
        if (handle) {
            vmaDestroyBuffer(allocator, handle, allocation);
        }
    }

    void upload(const VulkanRuntime &runtime, std::span<const std::byte> bytes, VkBufferUsageFlags usage,
                const char *name) {
        allocator = runtime.allocator;
        auto create = vkInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size = bytes.size();
        create.usage = usage;
        auto memory = star::rhi::vulkanAllocationInfo(star::rhi::MemoryType::Upload);
        check(vmaCreateBuffer(allocator, &create, &memory, &handle, &allocation, nullptr), "Allocate buffer");
        check(vmaCopyMemoryToAllocation(allocator, bytes.data(), allocation, 0, bytes.size()), "Upload buffer");
        runtime.nameObject(VK_OBJECT_TYPE_BUFFER, vulkanHandle(handle), name);
    }

    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct Image {
    Image() = default;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    ~Image() {
        if (view) {
            runtime->vk.vkDestroyImageView(runtime->device, view, nullptr);
        }
        if (handle) {
            vmaDestroyImage(runtime->allocator, handle, allocation);
        }
    }

    void create(const VulkanRuntime &owner, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                const char *name) {
        runtime = &owner;
        auto image = vkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {extent.width, extent.height, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = usage;
        const auto memory = star::rhi::vulkanAllocationInfo(star::rhi::MemoryType::GpuOnly);
        check(vmaCreateImage(owner.allocator, &image, &memory, &handle, &allocation, nullptr), "Allocate image");
        auto view_create = vkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view_create.image = handle;
        view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create.format = format;
        view_create.subresourceRange = range;
        check(owner.vk.vkCreateImageView(owner.device, &view_create, nullptr, &view), "Create image view");
        owner.nameObject(VK_OBJECT_TYPE_IMAGE, vulkanHandle(handle), name);
        owner.nameObject(VK_OBJECT_TYPE_IMAGE_VIEW, vulkanHandle(view), name);
    }

    static constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VulkanRuntime *runtime = nullptr;
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct Vertex {
    float position[2];
    float uv[2];
};

constexpr std::array vertices{
    Vertex{{-.56F, -.56F}, {0.0F, 0.0F}}, Vertex{{.56F, -.56F}, {1.0F, 0.0F}}, Vertex{{.56F, .56F}, {1.0F, 1.0F}},
    Vertex{{.56F, .56F}, {1.0F, 1.0F}},   Vertex{{-.56F, .56F}, {0.0F, 1.0F}}, Vertex{{-.56F, -.56F}, {0.0F, 0.0F}},
};

class MeshSample final {
  public:
    MeshSample() = default;
    MeshSample(const MeshSample &) = delete;
    MeshSample &operator=(const MeshSample &) = delete;
    ~MeshSample() {
        if (!m_gpu.device) {
            return;
        }
        const auto &vk = m_gpu.vk;
        const auto device = m_gpu.device;
        vk.vkDeviceWaitIdle(device);
        vk.vkDestroyPipeline(device, m_pipeline, nullptr);
        vk.vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
        vk.vkDestroyDescriptorPool(device, m_descriptors, nullptr);
        vk.vkDestroyDescriptorSetLayout(device, m_descriptor_layout, nullptr);
        vk.vkDestroySampler(device, m_sampler, nullptr);
        vk.vkDestroyQueryPool(device, m_queries, nullptr);
        vk.vkDestroyFence(device, m_fence, nullptr);
        vk.vkDestroySemaphore(device, m_acquired, nullptr);
        for (VkSemaphore semaphore : m_rendered) {
            vk.vkDestroySemaphore(device, semaphore, nullptr);
        }
        vk.vkDestroyCommandPool(device, m_commands, nullptr);
        vk.vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    }

    void initialize(SDL_Window *window, bool validation, const std::filesystem::path &assets) {
        if (const auto result = m_gpu.initialize(validation, window); !result) {
            throw std::runtime_error("Initialize Vulkan device returned " +
                                     std::to_string(static_cast<int>(result.error())));
        }
        createSwapchain(window);
        createGraph();
        createCommandObjects();
        uploadMesh();
        createDescriptors();
        createPipeline(assets);
        std::printf("Vulkan device %s, SDL driver %s, validation %s\n", m_gpu.properties.deviceName,
                    SDL_GetCurrentVideoDriver(), m_gpu.validationEnabled ? "enabled" : "disabled");
    }

    bool draw(float seconds) {
        const auto &vk = m_gpu.vk;
        std::uint32_t index = 0;
        const VkResult acquired =
            vk.vkAcquireNextImageKHR(m_gpu.device, m_swapchain, UINT64_MAX, m_acquired, VK_NULL_HANDLE, &index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            return false;
        }
        if (acquired != VK_SUBOPTIMAL_KHR) {
            check(acquired, "Acquire swapchain image");
        }
        beginCommands();
        if (m_queries) {
            vk.vkCmdResetQueryPool(m_buffer, m_queries, 0, 4);
        }
        constexpr star::rhi::VulkanTextureSync present{VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
        constexpr star::rhi::VulkanTextureSync acquired_image{VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
                                                              VK_IMAGE_LAYOUT_UNDEFINED};
        transitionVulkanImage(m_gpu, m_buffer, m_backbuffers[index], Image::range, acquired_image,
                              vulkanTextureSync(TextureState::CopyDestination));
        transitionVulkanImage(m_gpu, m_buffer, m_target.handle, Image::range,
                              vulkanTextureSync(m_drawn ? TextureState::CopySource : TextureState::Undefined),
                              vulkanTextureSync(TextureState::ColorAttachment));

        std::uint32_t query = 0;
        for (const auto pass : m_compiled.passOrder) {
            for (const auto &barrier : m_compiled.barriers) {
                if (barrier.resource == m_color && barrier.fromPass == m_mesh_pass && barrier.toPass == pass) {
                    transitionVulkanImage(m_gpu, m_buffer, m_target.handle, Image::range,
                                          vulkanTextureSync(barrier.before), vulkanTextureSync(barrier.after));
                }
            }
            m_gpu.beginLabel(m_buffer, m_graph.getPassName(pass).data());
            timestamp(query++);
            if (pass == m_mesh_pass) {
                recordMesh(seconds);
            } else {
                recordCopy(m_backbuffers[index]);
            }
            timestamp(query++);
            m_gpu.endLabel(m_buffer);
        }
        transitionVulkanImage(m_gpu, m_buffer, m_backbuffers[index], Image::range,
                              vulkanTextureSync(TextureState::CopyDestination), present);
        finishCommands(m_acquired, m_rendered[index]);

        auto info = vkInfo<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &m_rendered[index];
        info.swapchainCount = 1;
        info.pSwapchains = &m_swapchain;
        info.pImageIndices = &index;
        const VkResult presented = vk.vkQueuePresentKHR(m_gpu.queue, &info);
        if (m_gpu.validationErrors.load(std::memory_order_relaxed) != 0) {
            throw std::runtime_error("Vulkan validation reported errors");
        }
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR) {
            return false;
        }
        check(presented, "Present image");
        if (m_queries && !m_drawn) {
            std::array<std::uint64_t, 4> ticks{};
            check(vk.vkGetQueryPoolResults(m_gpu.device, m_queries, 0, 4, sizeof(ticks), ticks.data(), sizeof(ticks[0]),
                                           VK_QUERY_RESULT_64_BIT),
                  "Read GPU timestamps");
            const auto valid_bits = m_gpu.queueProperties.timestampValidBits;
            const auto mask = valid_bits == 64 ? UINT64_MAX : (std::uint64_t{1} << valid_bits) - 1;
            const auto nanos = [&](std::size_t end) {
                return static_cast<double>((ticks[end] - ticks[end - 1]) & mask) *
                       m_gpu.properties.limits.timestampPeriod;
            };
            std::printf("GPU timestamps %.0f ns for mesh, %.0f ns for copy\n", nanos(1), nanos(3));
        }
        m_drawn = true;
        return true;
    }

  private:
    void createSwapchain(SDL_Window *window) {
        const auto &inst = m_gpu.inst;
        VkSurfaceCapabilitiesKHR capabilities{};
        check(inst.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu.physicalDevice, m_gpu.surface, &capabilities),
              "Read surface capabilities");
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
            throw std::runtime_error("Surface does not support copying into swapchain images");
        }
        std::uint32_t count = 0;
        check(inst.vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu.physicalDevice, m_gpu.surface, &count, nullptr),
              "Count surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(inst.vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu.physicalDevice, m_gpu.surface, &count, formats.data()),
              "Read surface formats");
        const auto preferred = std::find_if(formats.begin(), formats.end(), [](const auto &format) {
            return format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM ||
                   format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB;
        });
        if (preferred == formats.end()) {
            throw std::runtime_error("No RGBA8 or BGRA8 surface format");
        }
        m_format = preferred->format;
        m_extent = capabilities.currentExtent;
        if (m_extent.width == UINT32_MAX) {
            int width = 0;
            int height = 0;
            if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
                throw std::runtime_error(SDL_GetError());
            }
            m_extent.width = std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
            m_extent.height = std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);
        }
        auto info = vkInfo<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        info.surface = m_gpu.surface;
        info.minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount != 0) {
            info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
        }
        info.imageFormat = m_format;
        info.imageColorSpace = preferred->colorSpace;
        info.imageExtent = m_extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.preTransform = capabilities.currentTransform;
        constexpr std::array alpha_modes{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                         VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                         VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
        info.compositeAlpha = *std::find_if(alpha_modes.begin(), alpha_modes.end(), [&](auto mode) {
            return (capabilities.supportedCompositeAlpha & mode) != 0;
        });
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        check(m_gpu.vk.vkCreateSwapchainKHR(m_gpu.device, &info, nullptr, &m_swapchain), "Create swapchain");
        check(m_gpu.vk.vkGetSwapchainImagesKHR(m_gpu.device, m_swapchain, &count, nullptr), "Count swapchain images");
        m_backbuffers.resize(count);
        check(m_gpu.vk.vkGetSwapchainImagesKHR(m_gpu.device, m_swapchain, &count, m_backbuffers.data()),
              "Read swapchain images");
        for (std::size_t i = 0; i < m_backbuffers.size(); ++i) {
            const auto name = "Swapchain Image " + std::to_string(i);
            m_gpu.nameObject(VK_OBJECT_TYPE_IMAGE, vulkanHandle(m_backbuffers[i]), name.c_str());
        }
    }

    void createGraph() {
        using namespace star::rhi;
        const auto color = m_graph.createTexture(
            "Mesh Color", {
                              .width = m_extent.width,
                              .height = m_extent.height,
                              .format = m_format == VK_FORMAT_B8G8R8A8_UNORM || m_format == VK_FORMAT_B8G8R8A8_SRGB
                                            ? TextureFormat::Bgra8Unorm
                                            : TextureFormat::Rgba8Unorm,
                              .usage = TextureUsage::RenderTarget | TextureUsage::CopySource,
                          });
        const auto screen = m_graph.createTexture("Swapchain Image", {
                                                                         .width = m_extent.width,
                                                                         .height = m_extent.height,
                                                                         .imported = true,
                                                                     });
        if (!color || !screen) {
            throw std::runtime_error("Declare render graph textures");
        }
        m_color = *color;
        const std::array mesh_uses{RenderResourceUse{*color, true, TextureState::ColorAttachment}};
        const std::array copy_uses{RenderResourceUse{*color, false, TextureState::CopySource},
                                   RenderResourceUse{*screen, true, TextureState::CopyDestination}};
        const auto mesh = m_graph.addPass({"Textured Mesh", RenderPassType::Raster, mesh_uses});
        const auto copy = m_graph.addPass({"Copy to Swapchain", RenderPassType::Copy, copy_uses});
        if (!mesh || !copy) {
            throw std::runtime_error("Declare render graph passes");
        }
        m_mesh_pass = *mesh;
        const std::array outputs{*screen};
        auto compiled = m_graph.compile(outputs);
        if (!compiled || compiled->transientTextures.size() != 1) {
            throw std::runtime_error("Compile render graph");
        }
        m_compiled = std::move(*compiled);
        const auto &target = m_compiled.transientTextures.front();
        m_target.create(m_gpu, {target.description.width, target.description.height}, m_format,
                        vulkanTextureUsage(target.description.usage), target.name.c_str());
    }

    void createCommandObjects() {
        const auto &vk = m_gpu.vk;
        const auto device = m_gpu.device;
        auto pool = vkInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = m_gpu.queueFamily;
        check(vk.vkCreateCommandPool(device, &pool, nullptr, &m_commands), "Create command pool");
        auto allocation = vkInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocation.commandPool = m_commands;
        allocation.commandBufferCount = 1;
        check(vk.vkAllocateCommandBuffers(device, &allocation, &m_buffer), "Allocate command buffer");
        const auto fence = vkInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        check(vk.vkCreateFence(device, &fence, nullptr, &m_fence), "Create fence");
        const auto semaphore = vkInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        check(vk.vkCreateSemaphore(device, &semaphore, nullptr, &m_acquired), "Create acquire semaphore");
        m_rendered.resize(m_backbuffers.size());
        for (VkSemaphore &rendered : m_rendered) {
            check(vk.vkCreateSemaphore(device, &semaphore, nullptr, &rendered), "Create render semaphore");
        }
        if (m_gpu.queueProperties.timestampValidBits != 0) {
            auto query = vkInfo<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = 4;
            check(vk.vkCreateQueryPool(device, &query, nullptr, &m_queries), "Create timestamp queries");
        }
    }

    void beginCommands() {
        check(m_gpu.vk.vkResetCommandBuffer(m_buffer, 0), "Reset command buffer");
        const auto info = vkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        check(m_gpu.vk.vkBeginCommandBuffer(m_buffer, &info), "Begin command buffer");
    }

    void finishCommands(VkSemaphore wait = VK_NULL_HANDLE, VkSemaphore signal = VK_NULL_HANDLE) {
        const auto &vk = m_gpu.vk;
        check(vk.vkEndCommandBuffer(m_buffer), "End command buffer");
        auto commands = vkInfo<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
        commands.commandBuffer = m_buffer;
        auto wait_info = vkInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        wait_info.semaphore = wait;
        wait_info.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        auto signal_info = vkInfo<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signal_info.semaphore = signal;
        signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        auto submit = vkInfo<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commands;
        submit.waitSemaphoreInfoCount = wait ? 1U : 0U;
        submit.pWaitSemaphoreInfos = &wait_info;
        submit.signalSemaphoreInfoCount = signal ? 1U : 0U;
        submit.pSignalSemaphoreInfos = &signal_info;
        check(vk.vkResetFences(m_gpu.device, 1, &m_fence), "Reset fence");
        check(vk.vkQueueSubmit2(m_gpu.queue, 1, &submit, m_fence), "Submit render commands");
        check(vk.vkWaitForFences(m_gpu.device, 1, &m_fence, VK_TRUE, UINT64_MAX), "Wait for render fence");
    }

    void uploadMesh() {
        m_vertices.upload(m_gpu, std::as_bytes(std::span(vertices)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          "Mesh Vertices");
        constexpr std::uint32_t size = 64;
        std::array<std::array<std::uint8_t, 4>, size * size> pixels{};
        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
                pixels[y * size + x] = ((x / 8 + y / 8) % 2) == 0 ? std::array<std::uint8_t, 4>{239, 159, 75, 255}
                                                                  : std::array<std::uint8_t, 4>{43, 88, 148, 255};
            }
        }
        Buffer upload;
        upload.upload(m_gpu, std::as_bytes(std::span(pixels)), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "Checkerboard Upload");
        m_texture.create(m_gpu, {size, size}, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "Checkerboard");
        beginCommands();
        transitionVulkanImage(m_gpu, m_buffer, m_texture.handle, Image::range,
                              vulkanTextureSync(TextureState::Undefined),
                              vulkanTextureSync(TextureState::CopyDestination));
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {size, size, 1};
        m_gpu.vk.vkCmdCopyBufferToImage(m_buffer, upload.handle, m_texture.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        1, &copy);
        transitionVulkanImage(m_gpu, m_buffer, m_texture.handle, Image::range,
                              vulkanTextureSync(TextureState::CopyDestination),
                              vulkanTextureSync(TextureState::Sampled));
        finishCommands();
    }

    void createDescriptors() {
        const auto &vk = m_gpu.vk;
        const auto device = m_gpu.device;
        auto sampler = vkInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        sampler.magFilter = VK_FILTER_NEAREST;
        sampler.minFilter = VK_FILTER_NEAREST;
        check(vk.vkCreateSampler(device, &sampler, nullptr, &m_sampler), "Create sampler");
        m_gpu.nameObject(VK_OBJECT_TYPE_SAMPLER, vulkanHandle(m_sampler), "Checkerboard Sampler");
        constexpr std::array bindings{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        auto layout = vkInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layout.pBindings = bindings.data();
        check(vk.vkCreateDescriptorSetLayout(device, &layout, nullptr, &m_descriptor_layout),
              "Create descriptor layout");
        constexpr std::array sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                   VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
        auto pool = vkInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool.maxSets = 1;
        pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        check(vk.vkCreateDescriptorPool(device, &pool, nullptr, &m_descriptors), "Create descriptor pool");
        auto allocate = vkInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocate.descriptorPool = m_descriptors;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &m_descriptor_layout;
        check(vk.vkAllocateDescriptorSets(device, &allocate, &m_descriptor_set), "Allocate descriptors");
        const std::array images{
            VkDescriptorImageInfo{VK_NULL_HANDLE, m_texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            VkDescriptorImageInfo{m_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
        };
        std::array<VkWriteDescriptorSet, bindings.size()> writes{};
        for (std::uint32_t i = 0; i < writes.size(); ++i) {
            writes[i] = vkInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            writes[i].dstSet = m_descriptor_set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bindings[i].descriptorType;
            writes[i].pImageInfo = &images[i];
        }
        vk.vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    VkShaderModule loadShader(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto length = input.tellg();
        if (length <= 0 || length % sizeof(std::uint32_t) != 0) {
            throw std::runtime_error("Invalid shader file " + path.string());
        }
        std::vector<std::uint32_t> words(static_cast<std::size_t>(length) / sizeof(std::uint32_t));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char *>(words.data()), length) || words.front() != 0x07230203) {
            throw std::runtime_error("Invalid SPIR-V in " + path.string());
        }
        auto info = vkInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        info.codeSize = static_cast<std::size_t>(length);
        info.pCode = words.data();
        VkShaderModule shader = VK_NULL_HANDLE;
        check(m_gpu.vk.vkCreateShaderModule(m_gpu.device, &info, nullptr, &shader), "Create shader module");
        return shader;
    }

    void createPipeline(const std::filesystem::path &assets) {
        const auto &vk = m_gpu.vk;
        const auto device = m_gpu.device;
        const auto destroy_shader = [&](VkShaderModule shader) { vk.vkDestroyShaderModule(device, shader, nullptr); };
        using Shader = std::unique_ptr<std::remove_pointer_t<VkShaderModule>, decltype(destroy_shader)>;
        const Shader vertex(loadShader(assets / "mesh-vertex.spv"), destroy_shader);
        const Shader fragment(loadShader(assets / "mesh-fragment.spv"), destroy_shader);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0] = vkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex.get();
        stages[0].pName = "main";
        stages[1] = stages[0];
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment.get();
        const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        constexpr std::array attributes{
            VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
            VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        };
        auto vertex_input =
            vkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();
        auto assembly =
            vkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        const VkViewport viewport{0.0F, 0.0F, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height),
                                  0.0F, 1.0F};
        const VkRect2D scissor{{0, 0}, m_extent};
        auto viewport_state =
            vkInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;
        auto raster =
            vkInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.lineWidth = 1.0F;
        auto samples =
            vkInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState color{};
        color.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        auto blend =
            vkInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blend.attachmentCount = 1;
        blend.pAttachments = &color;
        const VkPushConstantRange constants{VK_SHADER_STAGE_VERTEX_BIT, 0, 3 * sizeof(float)};
        auto layout = vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_descriptor_layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &constants;
        check(vk.vkCreatePipelineLayout(device, &layout, nullptr, &m_pipeline_layout), "Create pipeline layout");
        auto rendering = vkInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &m_format;
        auto pipeline = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        pipeline.pNext = &rendering;
        pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
        pipeline.pStages = stages.data();
        pipeline.pVertexInputState = &vertex_input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport_state;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pColorBlendState = &blend;
        pipeline.layout = m_pipeline_layout;
        check(vk.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline),
              "Create mesh pipeline");
        m_gpu.nameObject(VK_OBJECT_TYPE_PIPELINE, vulkanHandle(m_pipeline), "Textured Mesh Pipeline");
    }

    void recordMesh(float seconds) {
        const auto &vk = m_gpu.vk;
        auto color = vkInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
        color.imageView = m_target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{.045F, .065F, .1F, 1.0F}};
        auto rendering = vkInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.extent = m_extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vk.vkCmdBeginRendering(m_buffer, &rendering);
        vk.vkCmdBindPipeline(m_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vk.vkCmdBindDescriptorSets(m_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1,
                                   &m_descriptor_set, 0, nullptr);
        const VkDeviceSize offset = 0;
        vk.vkCmdBindVertexBuffers(m_buffer, 0, 1, &m_vertices.handle, &offset);
        const std::array transform{std::cos(seconds), std::sin(seconds),
                                   static_cast<float>(m_extent.height) / static_cast<float>(m_extent.width)};
        vk.vkCmdPushConstants(m_buffer, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(transform),
                              transform.data());
        vk.vkCmdDraw(m_buffer, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
        vk.vkCmdEndRendering(m_buffer);
    }

    void recordCopy(VkImage destination) {
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {m_extent.width, m_extent.height, 1};
        m_gpu.vk.vkCmdCopyImage(m_buffer, m_target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    void timestamp(std::uint32_t index) {
        if (m_queries) {
            m_gpu.vk.vkCmdWriteTimestamp2(m_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, index);
        }
    }

    VulkanRuntime m_gpu;
    Buffer m_vertices;
    Image m_texture;
    Image m_target;
    star::rhi::RenderGraph m_graph;
    star::rhi::CompiledRenderGraph m_compiled;
    star::rhi::RenderTextureHandle m_color;
    star::rhi::RenderPassHandle m_mesh_pass;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_backbuffers;
    VkCommandPool m_commands = VK_NULL_HANDLE;
    VkCommandBuffer m_buffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkSemaphore m_acquired = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_rendered;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptors = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    bool m_drawn = false;
};

struct SdlLifetime {
    ~SdlLifetime() { SDL_Quit(); }
};

} // namespace

int main(int argc, char **argv) {
    try {
#if defined(STAR_DEBUG) || defined(STAR_DEVELOPMENT)
        bool validation = true;
#else
        bool validation = false;
#endif
        std::uint32_t frame_limit = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string_view option(argv[i]);
            if (option == "--no-validation") {
                validation = false;
            } else if (option == "--validation") {
                validation = true;
            } else if (option.starts_with("--frames=")) {
                const auto count = option.substr(9);
                const auto result = std::from_chars(count.data(), count.data() + count.size(), frame_limit);
                if (result.ec != std::errc{} || result.ptr != count.data() + count.size() || frame_limit == 0) {
                    throw std::runtime_error("Expected a positive frame count");
                }
            } else {
                throw std::runtime_error("Unknown option " + std::string(option));
            }
        }
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(SDL_GetError());
        }
        const SdlLifetime sdl;
        const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Star Vulkan Mesh", 960, 640, SDL_WINDOW_VULKAN), SDL_DestroyWindow);
        if (!window) {
            throw std::runtime_error(SDL_GetError());
        }
        const char *base = SDL_GetBasePath();
        if (!base) {
            throw std::runtime_error(SDL_GetError());
        }
        const std::filesystem::path assets(reinterpret_cast<const char8_t *>(base));
        const auto start = SDL_GetTicks();
        std::unique_ptr<MeshSample> sample;
        std::uint32_t frames = 0;
        while (frame_limit == 0 || frames < frame_limit) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    return frame_limit == 0 || frames == frame_limit ? 0 : 1;
                }
            }
            if ((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) != 0) {
                SDL_Delay(16);
                continue;
            }
            if (!sample) {
                sample = std::make_unique<MeshSample>();
                sample->initialize(window.get(), validation, assets);
            }
            if (sample->draw(static_cast<float>(SDL_GetTicks() - start) / 1000.0F)) {
                ++frames;
            } else {
                sample.reset();
                SDL_Delay(16);
            }
        }
        std::printf("Presented %u frames\n", frames);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
