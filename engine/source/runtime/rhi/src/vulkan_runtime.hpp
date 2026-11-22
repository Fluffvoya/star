#pragma once

#include "rhi/rhi.hpp"

#include <vk_mem_alloc.h>

#include <atomic>
#include <type_traits>
#include <volk.h>

struct SDL_Window;

namespace star::rhi {

class VulkanRuntime final {
  public:
    VulkanRuntime() = default;
    ~VulkanRuntime();
    VulkanRuntime(const VulkanRuntime &) = delete;
    VulkanRuntime &operator=(const VulkanRuntime &) = delete;

    [[nodiscard]] std::expected<void, DeviceError> initialize(bool validation, SDL_Window *window = nullptr);
    void nameObject(VkObjectType type, std::uint64_t handle, const char *name) const noexcept;
    void beginLabel(VkCommandBuffer commands, const char *name) const noexcept;
    void endLabel(VkCommandBuffer commands) const noexcept;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkQueueFamilyProperties queueProperties{};
    VkPhysicalDeviceProperties properties{};
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VolkInstanceTable inst{};
    VolkDeviceTable vk{};
    bool validationEnabled = false;
    std::atomic_uint32_t validationErrors{0};
};

[[nodiscard]] DeviceError vulkanError(VkResult result) noexcept;
[[nodiscard]] VkFormat vulkanFormat(TextureFormat format) noexcept;
[[nodiscard]] VkImageUsageFlags vulkanTextureUsage(TextureUsage usage) noexcept;
[[nodiscard]] VmaAllocationCreateInfo vulkanAllocationInfo(MemoryType memory) noexcept;

struct VulkanTextureSync {
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

[[nodiscard]] VulkanTextureSync vulkanTextureSync(TextureState state) noexcept;
void transitionVulkanImage(const VulkanRuntime &runtime, VkCommandBuffer commands, VkImage image,
                           const VkImageSubresourceRange &range, VulkanTextureSync before,
                           VulkanTextureSync after) noexcept;

template <typename HandleType> std::uint64_t vulkanHandle(HandleType handle) noexcept {
    if constexpr (std::is_pointer_v<HandleType>) {
        return reinterpret_cast<std::uintptr_t>(handle);
    } else {
        return static_cast<std::uint64_t>(handle);
    }
}

} // namespace star::rhi
