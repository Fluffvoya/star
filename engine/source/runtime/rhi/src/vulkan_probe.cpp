#include "rhi/vulkan.hpp"

#include <SDL3/SDL_vulkan.h>

#include <cstring>
#include <array>
#include <algorithm>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace star::rhi {
namespace {

using VkFlags = std::uint32_t;
using VkBool32 = std::uint32_t;
using VkInstance = struct VkInstance_T *;
using VkPhysicalDevice = struct VkPhysicalDevice_T *;
using VkDevice = struct VkDevice_T *;
using VkQueue = struct VkQueue_T *;
using VkCommandPool = struct VkCommandPool_T *;
using VkCommandBuffer = struct VkCommandBuffer_T *;
using VkFence = struct VkFence_T *;
using VkBuffer = struct VkBuffer_T *;
using VkDeviceMemory = struct VkDeviceMemory_T *;
using VkImage = struct VkImage_T *;
using VkResult = std::int32_t;

constexpr VkResult vkSuccess = 0;
constexpr VkResult vkIncomplete = 5;
constexpr std::uint32_t vkApiVersion10 = (1U << 22U);
constexpr std::uint32_t vkApiVersion13 = (1U << 22U) | (3U << 12U);
constexpr std::uint32_t vkStructureTypeApplicationInfo = 0;
constexpr std::uint32_t vkStructureTypeInstanceCreateInfo = 1;
constexpr std::uint32_t vkStructureTypeDeviceQueueCreateInfo = 2;
constexpr std::uint32_t vkStructureTypeDeviceCreateInfo = 3;
constexpr std::uint32_t vkStructureTypeSubmitInfo = 4;
constexpr std::uint32_t vkStructureTypeFenceCreateInfo = 8;
constexpr std::uint32_t vkStructureTypeCommandPoolCreateInfo = 39;
constexpr std::uint32_t vkStructureTypeCommandBufferAllocateInfo = 40;
constexpr std::uint32_t vkStructureTypeCommandBufferBeginInfo = 42;
constexpr std::uint32_t vkStructureTypeMemoryAllocateInfo = 5;
constexpr std::uint32_t vkStructureTypeBufferCreateInfo = 12;
constexpr std::uint32_t vkStructureTypeImageCreateInfo = 14;
constexpr VkFlags vkQueueGraphicsBit = 1U;
constexpr VkFlags vkQueueComputeBit = 2U;
constexpr VkFlags vkQueueTransferBit = 4U;

struct VkApplicationInfo {
    std::uint32_t sType;
    const void *pNext;
    const char *pApplicationName;
    std::uint32_t applicationVersion;
    const char *pEngineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    std::uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
};

struct VkLayerProperties {
    char layerName[256];
    std::uint32_t specVersion;
    std::uint32_t implementationVersion;
    char description[256];
};

struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    std::uint32_t queueCount;
    std::uint32_t minImageTransferGranularity[3];
};

struct VkDeviceQueueCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    std::uint32_t queueFamilyIndex;
    std::uint32_t queueCount;
    const float *pQueuePriorities;
};

struct VkDeviceCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    std::uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo *pQueueCreateInfos;
    std::uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
    const void *pEnabledFeatures;
};

struct VkCommandPoolCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    std::uint32_t queueFamilyIndex;
};

struct VkCommandBufferAllocateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkCommandPool commandPool;
    std::uint32_t level;
    std::uint32_t commandBufferCount;
};

struct VkCommandBufferBeginInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    const void *pInheritanceInfo;
};

struct VkSubmitInfo {
    std::uint32_t sType;
    const void *pNext;
    std::uint32_t waitSemaphoreCount;
    const void *pWaitSemaphores;
    const void *pWaitDstStageMask;
    std::uint32_t commandBufferCount;
    const VkCommandBuffer *pCommandBuffers;
    std::uint32_t signalSemaphoreCount;
    const void *pSignalSemaphores;
};

struct VkFenceCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
};

struct VkBufferCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    std::uint64_t size;
    VkFlags usage;
    std::uint32_t sharingMode;
    std::uint32_t queueFamilyIndexCount;
    const std::uint32_t *pQueueFamilyIndices;
};

struct VkExtent3D {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t depth;
};

struct VkImageCreateInfo {
    std::uint32_t sType;
    const void *pNext;
    VkFlags flags;
    std::uint32_t imageType;
    std::uint32_t format;
    VkExtent3D extent;
    std::uint32_t mipLevels;
    std::uint32_t arrayLayers;
    std::uint32_t samples;
    std::uint32_t tiling;
    VkFlags usage;
    std::uint32_t sharingMode;
    std::uint32_t queueFamilyIndexCount;
    const std::uint32_t *pQueueFamilyIndices;
    std::uint32_t initialLayout;
};

struct VkMemoryRequirements {
    std::uint64_t size;
    std::uint64_t alignment;
    std::uint32_t memoryTypeBits;
};

struct VkMemoryAllocateInfo {
    std::uint32_t sType;
    const void *pNext;
    std::uint64_t allocationSize;
    std::uint32_t memoryTypeIndex;
};

struct VkMemoryType {
    VkFlags propertyFlags;
    std::uint32_t heapIndex;
};

struct VkMemoryHeap {
    std::uint64_t size;
    VkFlags flags;
};

struct VkPhysicalDeviceMemoryProperties {
    std::uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    std::uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};

using VkVoidFunction = void (*)();
using VkGetInstanceProcAddr = VkVoidFunction (*)(VkInstance instance, const char *name);
using VkEnumerateInstanceVersion = VkResult (*)(std::uint32_t *version);
using VkEnumerateInstanceLayerProperties = VkResult (*)(std::uint32_t *count, VkLayerProperties *properties);
using VkCreateInstance = VkResult (*)(const VkInstanceCreateInfo *createInfo, const void *allocator,
                                      VkInstance *instance);
using VkDestroyInstance = void (*)(VkInstance instance, const void *allocator);
using VkEnumeratePhysicalDevices = VkResult (*)(VkInstance instance, std::uint32_t *count,
                                                VkPhysicalDevice *devices);
using VkGetPhysicalDeviceQueueFamilyProperties = void (*)(VkPhysicalDevice physicalDevice, std::uint32_t *count,
                                                           VkQueueFamilyProperties *properties);
using VkCreateDevice = VkResult (*)(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *createInfo,
                                    const void *allocator, VkDevice *device);
using VkDestroyDevice = void (*)(VkDevice device, const void *allocator);
using VkGetDeviceQueue = void (*)(VkDevice device, std::uint32_t queueFamilyIndex, std::uint32_t queueIndex,
                                  VkQueue *queue);
using VkGetDeviceProcAddr = VkVoidFunction (*)(VkDevice device, const char *name);
using VkCreateCommandPool = VkResult (*)(VkDevice device, const VkCommandPoolCreateInfo *createInfo,
                                          const void *allocator, VkCommandPool *commandPool);
using VkDestroyCommandPool = void (*)(VkDevice device, VkCommandPool commandPool, const void *allocator);
using VkAllocateCommandBuffers = VkResult (*)(VkDevice device, const VkCommandBufferAllocateInfo *allocateInfo,
                                               VkCommandBuffer *commandBuffers);
using VkFreeCommandBuffers = void (*)(VkDevice device, VkCommandPool commandPool, std::uint32_t count,
                                      const VkCommandBuffer *commandBuffers);
using VkBeginCommandBuffer = VkResult (*)(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *beginInfo);
using VkEndCommandBuffer = VkResult (*)(VkCommandBuffer commandBuffer);
using VkCreateFence = VkResult (*)(VkDevice device, const VkFenceCreateInfo *createInfo, const void *allocator,
                                    VkFence *fence);
using VkDestroyFence = void (*)(VkDevice device, VkFence fence, const void *allocator);
using VkQueueSubmit = VkResult (*)(VkQueue queue, std::uint32_t submitCount, const VkSubmitInfo *submits,
                                    VkFence fence);
using VkWaitForFences = VkResult (*)(VkDevice device, std::uint32_t fenceCount, const VkFence *fences,
                                     VkBool32 waitAll, std::uint64_t timeout);
using VkCreateBuffer = VkResult (*)(VkDevice device, const VkBufferCreateInfo *createInfo, const void *allocator,
                                     VkBuffer *buffer);
using VkDestroyBuffer = void (*)(VkDevice device, VkBuffer buffer, const void *allocator);
using VkGetBufferMemoryRequirements = void (*)(VkDevice device, VkBuffer buffer, VkMemoryRequirements *requirements);
using VkAllocateMemory = VkResult (*)(VkDevice device, const VkMemoryAllocateInfo *allocateInfo, const void *allocator,
                                      VkDeviceMemory *memory);
using VkFreeMemory = void (*)(VkDevice device, VkDeviceMemory memory, const void *allocator);
using VkBindBufferMemory = VkResult (*)(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                        std::uint64_t memoryOffset);
using VkCreateImage = VkResult (*)(VkDevice device, const VkImageCreateInfo *createInfo, const void *allocator,
                                    VkImage *image);
using VkDestroyImage = void (*)(VkDevice device, VkImage image, const void *allocator);
using VkGetImageMemoryRequirements = void (*)(VkDevice device, VkImage image, VkMemoryRequirements *requirements);
using VkBindImageMemory = VkResult (*)(VkDevice device, VkImage image, VkDeviceMemory memory,
                                       std::uint64_t memoryOffset);
using VkGetPhysicalDeviceMemoryProperties = void (*)(VkPhysicalDevice physicalDevice,
                                                      VkPhysicalDeviceMemoryProperties *properties);

template <typename Function> [[nodiscard]] Function loadGlobal(VkGetInstanceProcAddr getProcAddr, const char *name) {
    return reinterpret_cast<Function>(getProcAddr(nullptr, name));
}

template <typename Function>
[[nodiscard]] Function loadInstance(VkGetInstanceProcAddr getProcAddr, VkInstance instance, const char *name) {
    return reinterpret_cast<Function>(getProcAddr(instance, name));
}

[[nodiscard]] bool hasValidationLayer(VkEnumerateInstanceLayerProperties enumerateLayers) {
    std::uint32_t count = 0;
    if (enumerateLayers(&count, nullptr) != vkSuccess || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers;
    try {
        layers.resize(count);
    } catch (...) {
        return false;
    }
    if (enumerateLayers(&count, layers.data()) != vkSuccess) {
        return false;
    }
    for (const VkLayerProperties &layer : layers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::expected<VulkanProbeInfo, VulkanProbeError> probeVulkan(const VulkanProbeOptions &options) {
    static std::mutex loadMutex;
    std::scoped_lock lock(loadMutex);

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        return std::unexpected(VulkanProbeError::LoaderUnavailable);
    }

    const auto unload = []() noexcept { SDL_Vulkan_UnloadLibrary(); };
    const auto getProcAddr = reinterpret_cast<VkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (getProcAddr == nullptr) {
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    const auto enumerateVersion = loadGlobal<VkEnumerateInstanceVersion>(getProcAddr, "vkEnumerateInstanceVersion");
    std::uint32_t apiVersion = vkApiVersion10;
    if (enumerateVersion != nullptr && enumerateVersion(&apiVersion) != vkSuccess) {
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }
    if (apiVersion > vkApiVersion13) {
        apiVersion = vkApiVersion13;
    }

    const auto enumerateLayers =
        loadGlobal<VkEnumerateInstanceLayerProperties>(getProcAddr, "vkEnumerateInstanceLayerProperties");
    const bool validationAvailable = enumerateLayers != nullptr && hasValidationLayer(enumerateLayers);
    if (options.requireValidationLayer && !validationAvailable) {
        unload();
        return std::unexpected(VulkanProbeError::ValidationLayerUnavailable);
    }

    std::string applicationName;
    std::string engineName;
    try {
        applicationName = options.applicationName;
        engineName = options.engineName;
    } catch (...) {
        unload();
        return std::unexpected(VulkanProbeError::OutOfMemory);
    }

    const char *validationLayerName = "VK_LAYER_KHRONOS_validation";

    const VkApplicationInfo applicationInfo{
        vkStructureTypeApplicationInfo,
        nullptr,
        applicationName.c_str(),
        1,
        engineName.c_str(),
        1,
        apiVersion,
    };
    const VkInstanceCreateInfo createInfo{
        vkStructureTypeInstanceCreateInfo,
        nullptr,
        0,
        &applicationInfo,
        options.requireValidationLayer ? 1U : 0U,
        options.requireValidationLayer ? &validationLayerName : nullptr,
        0,
        nullptr,
    };
    const auto createInstance = loadGlobal<VkCreateInstance>(getProcAddr, "vkCreateInstance");
    if (createInstance == nullptr) {
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    VkInstance instance = nullptr;
    if (createInstance(&createInfo, nullptr, &instance) != vkSuccess || instance == nullptr) {
        unload();
        return std::unexpected(VulkanProbeError::InstanceCreationFailed);
    }

    const auto destroyInstance = loadInstance<VkDestroyInstance>(getProcAddr, instance, "vkDestroyInstance");
    const auto enumeratePhysicalDevices =
        loadInstance<VkEnumeratePhysicalDevices>(getProcAddr, instance, "vkEnumeratePhysicalDevices");
    if (destroyInstance == nullptr || enumeratePhysicalDevices == nullptr) {
        if (destroyInstance != nullptr) {
            destroyInstance(instance, nullptr);
        }
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    std::uint32_t physicalDeviceCount = 0;
    const VkResult enumerateResult = enumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
    destroyInstance(instance, nullptr);
    unload();
    if (enumerateResult != vkSuccess && enumerateResult != vkIncomplete) {
        return std::unexpected(VulkanProbeError::PhysicalDeviceEnumerationFailed);
    }

    return VulkanProbeInfo{apiVersion, physicalDeviceCount, validationAvailable};
}

struct VulkanContext::Impl {
    VulkanDeviceInfo info;
    VkInstance instance = nullptr;
    VkDevice device = nullptr;
    VkQueue graphicsQueue = nullptr;
    VkPhysicalDevice physicalDevice = nullptr;
    VkDestroyInstance destroyInstance = nullptr;
    VkDestroyDevice destroyDevice = nullptr;
    VkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    bool loaderLoaded = false;
};

VulkanContext::VulkanContext(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

VulkanContext::~VulkanContext() {
    if (m_impl == nullptr) {
        return;
    }
    static std::mutex loadMutex;
    std::scoped_lock lock(loadMutex);
    if (m_impl->device != nullptr && m_impl->destroyDevice != nullptr) {
        m_impl->destroyDevice(m_impl->device, nullptr);
    }
    if (m_impl->instance != nullptr && m_impl->destroyInstance != nullptr) {
        m_impl->destroyInstance(m_impl->instance, nullptr);
    }
    if (m_impl->loaderLoaded) {
        SDL_Vulkan_UnloadLibrary();
    }
}

VulkanContext::VulkanContext(VulkanContext &&other) noexcept : m_impl(std::move(other.m_impl)) {}

VulkanContext &VulkanContext::operator=(VulkanContext &&other) noexcept {
    if (this != &other) {
        this->~VulkanContext();
        new (this) VulkanContext(std::move(other));
    }
    return *this;
}

const VulkanDeviceInfo &VulkanContext::getInfo() const noexcept {
    static const VulkanDeviceInfo invalidInfo{};
    return m_impl != nullptr ? m_impl->info : invalidInfo;
}

bool VulkanContext::isValid() const noexcept { return m_impl != nullptr && m_impl->device != nullptr; }

std::expected<void, VulkanProbeError> VulkanContext::submitEmptyCommandBuffer() noexcept {
    if (!isValid() || m_impl->getDeviceProcAddr == nullptr || m_impl->graphicsQueue == nullptr) {
        return std::unexpected(VulkanProbeError::SubmissionFailed);
    }
    const auto createCommandPool = reinterpret_cast<VkCreateCommandPool>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkCreateCommandPool"));
    const auto destroyCommandPool = reinterpret_cast<VkDestroyCommandPool>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkDestroyCommandPool"));
    const auto allocateCommandBuffers = reinterpret_cast<VkAllocateCommandBuffers>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkAllocateCommandBuffers"));
    const auto freeCommandBuffers = reinterpret_cast<VkFreeCommandBuffers>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkFreeCommandBuffers"));
    const auto beginCommandBuffer = reinterpret_cast<VkBeginCommandBuffer>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkBeginCommandBuffer"));
    const auto endCommandBuffer = reinterpret_cast<VkEndCommandBuffer>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkEndCommandBuffer"));
    const auto createFence = reinterpret_cast<VkCreateFence>(m_impl->getDeviceProcAddr(m_impl->device, "vkCreateFence"));
    const auto destroyFence = reinterpret_cast<VkDestroyFence>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkDestroyFence"));
    const auto queueSubmit = reinterpret_cast<VkQueueSubmit>(m_impl->getDeviceProcAddr(m_impl->device, "vkQueueSubmit"));
    const auto waitForFences = reinterpret_cast<VkWaitForFences>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkWaitForFences"));
    if (createCommandPool == nullptr || destroyCommandPool == nullptr || allocateCommandBuffers == nullptr ||
        freeCommandBuffers == nullptr || beginCommandBuffer == nullptr || endCommandBuffer == nullptr ||
        createFence == nullptr || destroyFence == nullptr || queueSubmit == nullptr || waitForFences == nullptr) {
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    const VkCommandPoolCreateInfo poolInfo{
        vkStructureTypeCommandPoolCreateInfo, nullptr, 0, m_impl->info.graphicsQueueFamily};
    VkCommandPool commandPool = nullptr;
    if (createCommandPool(m_impl->device, &poolInfo, nullptr, &commandPool) != vkSuccess || commandPool == nullptr) {
        return std::unexpected(VulkanProbeError::CommandCreationFailed);
    }
    const auto cleanupPool = [&]() noexcept { destroyCommandPool(m_impl->device, commandPool, nullptr); };

    const VkCommandBufferAllocateInfo allocateInfo{
        vkStructureTypeCommandBufferAllocateInfo, nullptr, commandPool, 0, 1};
    VkCommandBuffer commandBuffer = nullptr;
    if (allocateCommandBuffers(m_impl->device, &allocateInfo, &commandBuffer) != vkSuccess || commandBuffer == nullptr) {
        cleanupPool();
        return std::unexpected(VulkanProbeError::CommandCreationFailed);
    }
    const auto cleanupBuffer = [&]() noexcept {
        freeCommandBuffers(m_impl->device, commandPool, 1, &commandBuffer);
        cleanupPool();
    };

    const VkCommandBufferBeginInfo beginInfo{vkStructureTypeCommandBufferBeginInfo, nullptr, 0, nullptr};
    if (beginCommandBuffer(commandBuffer, &beginInfo) != vkSuccess || endCommandBuffer(commandBuffer) != vkSuccess) {
        cleanupBuffer();
        return std::unexpected(VulkanProbeError::CommandCreationFailed);
    }
    const VkFenceCreateInfo fenceInfo{vkStructureTypeFenceCreateInfo, nullptr, 0};
    VkFence fence = nullptr;
    if (createFence(m_impl->device, &fenceInfo, nullptr, &fence) != vkSuccess || fence == nullptr) {
        cleanupBuffer();
        return std::unexpected(VulkanProbeError::CommandCreationFailed);
    }
    const VkSubmitInfo submitInfo{vkStructureTypeSubmitInfo, nullptr, 0, nullptr, nullptr, 1, &commandBuffer, 0,
                                  nullptr};
    if (queueSubmit(m_impl->graphicsQueue, 1, &submitInfo, fence) != vkSuccess ||
        waitForFences(m_impl->device, 1, &fence, 1, std::numeric_limits<std::uint64_t>::max()) != vkSuccess) {
        destroyFence(m_impl->device, fence, nullptr);
        cleanupBuffer();
        return std::unexpected(VulkanProbeError::SubmissionFailed);
    }
    destroyFence(m_impl->device, fence, nullptr);
    cleanupBuffer();
    return {};
}

std::expected<void, VulkanProbeError> VulkanContext::createTransientBuffer(std::uint64_t size) noexcept {
    if (size == 0 || !isValid() || m_impl->getDeviceProcAddr == nullptr || m_impl->getInstanceProcAddr == nullptr ||
        m_impl->physicalDevice == nullptr) {
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    const auto createBuffer = reinterpret_cast<VkCreateBuffer>(m_impl->getDeviceProcAddr(m_impl->device, "vkCreateBuffer"));
    const auto destroyBuffer =
        reinterpret_cast<VkDestroyBuffer>(m_impl->getDeviceProcAddr(m_impl->device, "vkDestroyBuffer"));
    const auto getBufferMemoryRequirements = reinterpret_cast<VkGetBufferMemoryRequirements>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkGetBufferMemoryRequirements"));
    const auto allocateMemory =
        reinterpret_cast<VkAllocateMemory>(m_impl->getDeviceProcAddr(m_impl->device, "vkAllocateMemory"));
    const auto freeMemory = reinterpret_cast<VkFreeMemory>(m_impl->getDeviceProcAddr(m_impl->device, "vkFreeMemory"));
    const auto bindBufferMemory = reinterpret_cast<VkBindBufferMemory>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkBindBufferMemory"));
    const auto getMemoryProperties = reinterpret_cast<VkGetPhysicalDeviceMemoryProperties>(
        m_impl->getInstanceProcAddr(m_impl->instance, "vkGetPhysicalDeviceMemoryProperties"));
    if (createBuffer == nullptr || destroyBuffer == nullptr || getBufferMemoryRequirements == nullptr ||
        allocateMemory == nullptr || freeMemory == nullptr || bindBufferMemory == nullptr || getMemoryProperties == nullptr) {
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    const VkBufferCreateInfo bufferInfo{
        vkStructureTypeBufferCreateInfo,
        nullptr,
        0,
        size,
        0x00000080U,
        0,
        0,
        nullptr,
    };
    VkBuffer buffer = nullptr;
    if (createBuffer(m_impl->device, &bufferInfo, nullptr, &buffer) != vkSuccess || buffer == nullptr) {
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    const auto cleanupBuffer = [&]() noexcept { destroyBuffer(m_impl->device, buffer, nullptr); };

    VkMemoryRequirements requirements{};
    getBufferMemoryRequirements(m_impl->device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    getMemoryProperties(m_impl->physicalDevice, &memoryProperties);
    std::optional<std::uint32_t> memoryTypeIndex;
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) != 0) {
            memoryTypeIndex = index;
            break;
        }
    }
    if (!memoryTypeIndex.has_value()) {
        cleanupBuffer();
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }

    const VkMemoryAllocateInfo allocateInfo{
        vkStructureTypeMemoryAllocateInfo, nullptr, requirements.size, *memoryTypeIndex};
    VkDeviceMemory memory = nullptr;
    if (allocateMemory(m_impl->device, &allocateInfo, nullptr, &memory) != vkSuccess || memory == nullptr) {
        cleanupBuffer();
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    if (bindBufferMemory(m_impl->device, buffer, memory, 0) != vkSuccess) {
        freeMemory(m_impl->device, memory, nullptr);
        cleanupBuffer();
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    freeMemory(m_impl->device, memory, nullptr);
    cleanupBuffer();
    return {};
}

std::expected<void, VulkanProbeError> VulkanContext::createTransientTexture(std::uint32_t width,
                                                                              std::uint32_t height) noexcept {
    if (width == 0 || height == 0 || !isValid() || m_impl->getDeviceProcAddr == nullptr ||
        m_impl->getInstanceProcAddr == nullptr || m_impl->physicalDevice == nullptr) {
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    const auto createImage = reinterpret_cast<VkCreateImage>(m_impl->getDeviceProcAddr(m_impl->device, "vkCreateImage"));
    const auto destroyImage =
        reinterpret_cast<VkDestroyImage>(m_impl->getDeviceProcAddr(m_impl->device, "vkDestroyImage"));
    const auto getImageMemoryRequirements = reinterpret_cast<VkGetImageMemoryRequirements>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkGetImageMemoryRequirements"));
    const auto allocateMemory =
        reinterpret_cast<VkAllocateMemory>(m_impl->getDeviceProcAddr(m_impl->device, "vkAllocateMemory"));
    const auto freeMemory = reinterpret_cast<VkFreeMemory>(m_impl->getDeviceProcAddr(m_impl->device, "vkFreeMemory"));
    const auto bindImageMemory = reinterpret_cast<VkBindImageMemory>(
        m_impl->getDeviceProcAddr(m_impl->device, "vkBindImageMemory"));
    const auto getMemoryProperties = reinterpret_cast<VkGetPhysicalDeviceMemoryProperties>(
        m_impl->getInstanceProcAddr(m_impl->instance, "vkGetPhysicalDeviceMemoryProperties"));
    if (createImage == nullptr || destroyImage == nullptr || getImageMemoryRequirements == nullptr ||
        allocateMemory == nullptr || freeMemory == nullptr || bindImageMemory == nullptr || getMemoryProperties == nullptr) {
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    const VkImageCreateInfo imageInfo{
        vkStructureTypeImageCreateInfo,
        nullptr,
        0,
        1,
        37,
        VkExtent3D{width, height, 1},
        1,
        1,
        1,
        0,
        0x00000006U,
        0,
        0,
        nullptr,
        0,
    };
    VkImage image = nullptr;
    if (createImage(m_impl->device, &imageInfo, nullptr, &image) != vkSuccess || image == nullptr) {
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    const auto cleanupImage = [&]() noexcept { destroyImage(m_impl->device, image, nullptr); };

    VkMemoryRequirements requirements{};
    getImageMemoryRequirements(m_impl->device, image, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    getMemoryProperties(m_impl->physicalDevice, &memoryProperties);
    std::optional<std::uint32_t> memoryTypeIndex;
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1U << index)) != 0) {
            memoryTypeIndex = index;
            break;
        }
    }
    if (!memoryTypeIndex.has_value()) {
        cleanupImage();
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }

    const VkMemoryAllocateInfo allocateInfo{
        vkStructureTypeMemoryAllocateInfo, nullptr, requirements.size, *memoryTypeIndex};
    VkDeviceMemory memory = nullptr;
    if (allocateMemory(m_impl->device, &allocateInfo, nullptr, &memory) != vkSuccess || memory == nullptr) {
        cleanupImage();
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    if (bindImageMemory(m_impl->device, image, memory, 0) != vkSuccess) {
        freeMemory(m_impl->device, memory, nullptr);
        cleanupImage();
        return std::unexpected(VulkanProbeError::ResourceCreationFailed);
    }
    freeMemory(m_impl->device, memory, nullptr);
    cleanupImage();
    return {};
}

std::expected<VulkanContext, VulkanProbeError> createVulkanContext(const VulkanProbeOptions &options) {
    static std::mutex loadMutex;
    std::scoped_lock lock(loadMutex);

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        return std::unexpected(VulkanProbeError::LoaderUnavailable);
    }
    const auto unload = []() noexcept { SDL_Vulkan_UnloadLibrary(); };
    const auto getProcAddr = reinterpret_cast<VkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (getProcAddr == nullptr) {
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    const auto enumerateVersion = loadGlobal<VkEnumerateInstanceVersion>(getProcAddr, "vkEnumerateInstanceVersion");
    std::uint32_t apiVersion = vkApiVersion10;
    if (enumerateVersion != nullptr && enumerateVersion(&apiVersion) != vkSuccess) {
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }
    if (apiVersion > vkApiVersion13) {
        apiVersion = vkApiVersion13;
    }
    const auto enumerateLayers =
        loadGlobal<VkEnumerateInstanceLayerProperties>(getProcAddr, "vkEnumerateInstanceLayerProperties");
    const bool validationAvailable = enumerateLayers != nullptr && hasValidationLayer(enumerateLayers);
    if (options.requireValidationLayer && !validationAvailable) {
        unload();
        return std::unexpected(VulkanProbeError::ValidationLayerUnavailable);
    }

    std::string applicationName;
    std::string engineName;
    try {
        applicationName = options.applicationName;
        engineName = options.engineName;
    } catch (...) {
        unload();
        return std::unexpected(VulkanProbeError::OutOfMemory);
    }
    const char *validationLayerName = "VK_LAYER_KHRONOS_validation";
    const VkApplicationInfo applicationInfo{
        vkStructureTypeApplicationInfo, nullptr, applicationName.c_str(), 1, engineName.c_str(), 1, apiVersion};
    const VkInstanceCreateInfo instanceCreateInfo{
        vkStructureTypeInstanceCreateInfo,
        nullptr,
        0,
        &applicationInfo,
        options.requireValidationLayer ? 1U : 0U,
        options.requireValidationLayer ? &validationLayerName : nullptr,
        0,
        nullptr,
    };
    const auto createInstance = loadGlobal<VkCreateInstance>(getProcAddr, "vkCreateInstance");
    if (createInstance == nullptr) {
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }
    VkInstance instance = nullptr;
    if (createInstance(&instanceCreateInfo, nullptr, &instance) != vkSuccess || instance == nullptr) {
        unload();
        return std::unexpected(VulkanProbeError::InstanceCreationFailed);
    }

    const auto destroyInstance = loadInstance<VkDestroyInstance>(getProcAddr, instance, "vkDestroyInstance");
    const auto enumeratePhysicalDevices =
        loadInstance<VkEnumeratePhysicalDevices>(getProcAddr, instance, "vkEnumeratePhysicalDevices");
    const auto getQueueFamilyProperties = loadInstance<VkGetPhysicalDeviceQueueFamilyProperties>(
        getProcAddr, instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto createDevice = loadInstance<VkCreateDevice>(getProcAddr, instance, "vkCreateDevice");
    const auto destroyDevice = loadInstance<VkDestroyDevice>(getProcAddr, instance, "vkDestroyDevice");
    const auto getDeviceQueue = loadInstance<VkGetDeviceQueue>(getProcAddr, instance, "vkGetDeviceQueue");
    if (destroyInstance == nullptr || enumeratePhysicalDevices == nullptr || getQueueFamilyProperties == nullptr ||
        createDevice == nullptr || destroyDevice == nullptr || getDeviceQueue == nullptr) {
        if (destroyInstance != nullptr) {
            destroyInstance(instance, nullptr);
        }
        unload();
        return std::unexpected(VulkanProbeError::MissingEntryPoint);
    }

    std::uint32_t physicalDeviceCount = 0;
    if (enumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr) != vkSuccess || physicalDeviceCount == 0) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::PhysicalDeviceUnavailable);
    }
    std::vector<VkPhysicalDevice> physicalDevices;
    try {
        physicalDevices.resize(physicalDeviceCount);
    } catch (...) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::OutOfMemory);
    }
    if (enumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()) != vkSuccess) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::PhysicalDeviceUnavailable);
    }

    VkPhysicalDevice selectedPhysicalDevice = nullptr;
    std::uint32_t graphicsFamily = 0;
    std::uint32_t computeFamily = 0;
    std::uint32_t copyFamily = 0;
    for (VkPhysicalDevice physicalDevice : physicalDevices) {
        std::uint32_t familyCount = 0;
        getQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families;
        try {
            families.resize(familyCount);
        } catch (...) {
            destroyInstance(instance, nullptr);
            unload();
            return std::unexpected(VulkanProbeError::OutOfMemory);
        }
        getQueueFamilyProperties(physicalDevice, &familyCount, families.data());
        std::optional<std::uint32_t> graphics;
        std::optional<std::uint32_t> compute;
        std::optional<std::uint32_t> copy;
        for (std::uint32_t index = 0; index < familyCount; ++index) {
            const VkFlags flags = families[index].queueFlags;
            if (!graphics.has_value() && (flags & vkQueueGraphicsBit) != 0 && families[index].queueCount > 0) {
                graphics = index;
            }
            if (!compute.has_value() && (flags & vkQueueComputeBit) != 0 && families[index].queueCount > 0) {
                compute = index;
            }
            if (!copy.has_value() && (flags & vkQueueTransferBit) != 0 && families[index].queueCount > 0) {
                copy = index;
            }
        }
        if (graphics.has_value()) {
            selectedPhysicalDevice = physicalDevice;
            graphicsFamily = *graphics;
            computeFamily = compute.value_or(graphicsFamily);
            copyFamily = copy.value_or(computeFamily);
            break;
        }
    }
    if (selectedPhysicalDevice == nullptr) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::QueueFamilyUnavailable);
    }

    const float queuePriority = 1.0F;
    std::array<std::uint32_t, 3> familyIndices{graphicsFamily, computeFamily, copyFamily};
    std::vector<std::uint32_t> uniqueFamilies;
    try {
        for (std::uint32_t family : familyIndices) {
            if (std::find(uniqueFamilies.begin(), uniqueFamilies.end(), family) == uniqueFamilies.end()) {
                uniqueFamilies.push_back(family);
            }
        }
    } catch (...) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::OutOfMemory);
    }
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    try {
        queueCreateInfos.reserve(uniqueFamilies.size());
        for (std::uint32_t family : uniqueFamilies) {
            queueCreateInfos.push_back(
                VkDeviceQueueCreateInfo{vkStructureTypeDeviceQueueCreateInfo, nullptr, 0, family, 1, &queuePriority});
        }
    } catch (...) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::OutOfMemory);
    }
    const VkDeviceCreateInfo deviceCreateInfo{
        vkStructureTypeDeviceCreateInfo,
        nullptr,
        0,
        static_cast<std::uint32_t>(queueCreateInfos.size()),
        queueCreateInfos.data(),
        0,
        nullptr,
        0,
        nullptr,
        nullptr,
    };
    VkDevice device = nullptr;
    if (createDevice(selectedPhysicalDevice, &deviceCreateInfo, nullptr, &device) != vkSuccess || device == nullptr) {
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::DeviceCreationFailed);
    }
    VkQueue graphicsQueue = nullptr;
    getDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    if (graphicsQueue == nullptr) {
        destroyDevice(device, nullptr);
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::DeviceCreationFailed);
    }

    try {
        auto impl = std::make_unique<VulkanContext::Impl>();
        impl->info = VulkanDeviceInfo{apiVersion, physicalDeviceCount, graphicsFamily, computeFamily, copyFamily};
        impl->instance = instance;
        impl->device = device;
        impl->physicalDevice = selectedPhysicalDevice;
        impl->graphicsQueue = graphicsQueue;
        impl->destroyInstance = destroyInstance;
        impl->destroyDevice = destroyDevice;
        impl->getInstanceProcAddr = getProcAddr;
        impl->getDeviceProcAddr = loadInstance<VkGetDeviceProcAddr>(getProcAddr, instance, "vkGetDeviceProcAddr");
        if (impl->getDeviceProcAddr == nullptr) {
            destroyDevice(device, nullptr);
            destroyInstance(instance, nullptr);
            unload();
            return std::unexpected(VulkanProbeError::MissingEntryPoint);
        }
        impl->loaderLoaded = true;
        return VulkanContext(std::move(impl));
    } catch (...) {
        destroyDevice(device, nullptr);
        destroyInstance(instance, nullptr);
        unload();
        return std::unexpected(VulkanProbeError::OutOfMemory);
    }
}

} // namespace star::rhi
