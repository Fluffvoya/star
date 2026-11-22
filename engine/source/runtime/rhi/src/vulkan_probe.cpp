#include "rhi/vulkan.hpp"

#include <SDL3/SDL_vulkan.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace star::rhi {
namespace {

using VkFlags = std::uint32_t;
using VkInstance = struct VkInstance_T *;
using VkPhysicalDevice = struct VkPhysicalDevice_T *;
using VkResult = std::int32_t;

constexpr VkResult vkSuccess = 0;
constexpr VkResult vkIncomplete = 5;
constexpr std::uint32_t vkApiVersion10 = (1U << 22U);
constexpr std::uint32_t vkApiVersion13 = (1U << 22U) | (3U << 12U);
constexpr std::uint32_t vkStructureTypeApplicationInfo = 0;
constexpr std::uint32_t vkStructureTypeInstanceCreateInfo = 1;

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

using VkVoidFunction = void (*)();
using VkGetInstanceProcAddr = VkVoidFunction (*)(VkInstance instance, const char *name);
using VkEnumerateInstanceVersion = VkResult (*)(std::uint32_t *version);
using VkEnumerateInstanceLayerProperties = VkResult (*)(std::uint32_t *count, VkLayerProperties *properties);
using VkCreateInstance = VkResult (*)(const VkInstanceCreateInfo *createInfo, const void *allocator,
                                      VkInstance *instance);
using VkDestroyInstance = void (*)(VkInstance instance, const void *allocator);
using VkEnumeratePhysicalDevices = VkResult (*)(VkInstance instance, std::uint32_t *count,
                                                VkPhysicalDevice *devices);

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

} // namespace star::rhi
