#include "rhi/vulkan.hpp"

#include <SDL3/SDL_vulkan.h>

#include <cstring>
#include <array>
#include <algorithm>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace star::rhi {
namespace {

using VkFlags = std::uint32_t;
using VkInstance = struct VkInstance_T *;
using VkPhysicalDevice = struct VkPhysicalDevice_T *;
using VkDevice = struct VkDevice_T *;
using VkQueue = struct VkQueue_T *;
using VkResult = std::int32_t;

constexpr VkResult vkSuccess = 0;
constexpr VkResult vkIncomplete = 5;
constexpr std::uint32_t vkApiVersion10 = (1U << 22U);
constexpr std::uint32_t vkApiVersion13 = (1U << 22U) | (3U << 12U);
constexpr std::uint32_t vkStructureTypeApplicationInfo = 0;
constexpr std::uint32_t vkStructureTypeInstanceCreateInfo = 1;
constexpr std::uint32_t vkStructureTypeDeviceQueueCreateInfo = 2;
constexpr std::uint32_t vkStructureTypeDeviceCreateInfo = 3;
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
    VkDestroyInstance destroyInstance = nullptr;
    VkDestroyDevice destroyDevice = nullptr;
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
        impl->destroyInstance = destroyInstance;
        impl->destroyDevice = destroyDevice;
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
