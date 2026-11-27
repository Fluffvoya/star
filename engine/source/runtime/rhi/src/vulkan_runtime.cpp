#include "vulkan_runtime.hpp"

#include "core/log.hpp"
#include "device_selection.hpp"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

namespace star::rhi {
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL validationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT type,
                                                 const VkDebugUtilsMessengerCallbackDataEXT *message,
                                                 void *user) noexcept {
    if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT &&
        (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0) {
        static_cast<VulkanRuntime *>(user)->validationErrors.fetch_add(1, std::memory_order_relaxed);
    }
    try {
        star::log::write(LogCategory("Vulkan"),
                         severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? LogLevel::Error
                                                                                   : LogLevel::Warning,
                         message->pMessage);
    } catch (...) {
        // Exceptions cannot cross a Vulkan callback.
    }
    return VK_FALSE;
}

std::mutex volk_mutex;

} // namespace

VulkanRuntime::~VulkanRuntime() {
    if (device) {
        vk.vkDeviceWaitIdle(device);
    }
    if (allocator) {
        vmaDestroyAllocator(allocator);
    }
    if (device) {
        vk.vkDestroyDevice(device, nullptr);
    }
    if (surface) {
        inst.vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    if (messenger) {
        inst.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    }
    if (instance) {
        inst.vkDestroyInstance(instance, nullptr);
    }
}

std::expected<void, DeviceError> VulkanRuntime::initialize(bool validation, SDL_Window *window) {
    try {
        // volk's bootstrap pointers are shared. All operational calls use per-object tables.
        std::scoped_lock lock(volk_mutex);
        static const VkResult loader = volkInitialize();
        if (loader != VK_SUCCESS || volkGetInstanceVersion() < VK_API_VERSION_1_3) {
            return std::unexpected(DeviceError::BackendUnavailable);
        }
        std::vector<const char *> extensions;
        if (window != nullptr) {
            Uint32 count = 0;
            const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&count);
            if (!sdl_extensions) {
                return std::unexpected(DeviceError::BackendUnavailable);
            }
            extensions.assign(sdl_extensions, sdl_extensions + count);
        }
        std::uint32_t extension_count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr) != VK_SUCCESS) {
            return std::unexpected(DeviceError::BackendFailure);
        }
        std::vector<VkExtensionProperties> available_extensions(extension_count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available_extensions.data()) !=
            VK_SUCCESS) {
            return std::unexpected(DeviceError::BackendFailure);
        }
        const bool debug_utils = std::any_of(available_extensions.begin(), available_extensions.end(), [](auto &entry) {
            return std::strcmp(entry.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
        });
        if (debug_utils) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        constexpr const char *validation_layer = "VK_LAYER_KHRONOS_validation";
        if (validation) {
            std::uint32_t count = 0;
            if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
                return std::unexpected(DeviceError::BackendFailure);
            }
            std::vector<VkLayerProperties> layers(count);
            if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
                return std::unexpected(DeviceError::BackendFailure);
            }
            validationEnabled = debug_utils && std::any_of(layers.begin(), layers.end(), [](auto &entry) {
                                    return std::strcmp(entry.layerName, validation_layer) == 0;
                                });
            if (!validationEnabled) {
                return std::unexpected(DeviceError::BackendUnavailable);
            }
        }

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "Star Engine";
        app.pEngineName = "Star Engine";
        app.apiVersion = VK_API_VERSION_1_3;
        VkDebugUtilsMessengerCreateInfoEXT debug{};
        debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = validationMessage;
        debug.pUserData = this;
        VkInstanceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create.pApplicationInfo = &app;
        create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        create.ppEnabledExtensionNames = extensions.data();
        if (validationEnabled) {
            create.pNext = &debug;
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &validation_layer;
        }
        if (const VkResult result = vkCreateInstance(&create, nullptr, &instance); result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        volkLoadInstanceTable(&inst, instance);
        if (validationEnabled &&
            inst.vkCreateDebugUtilsMessengerEXT(instance, &debug, nullptr, &messenger) != VK_SUCCESS) {
            return std::unexpected(DeviceError::BackendFailure);
        }
        if (window && !SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            return std::unexpected(DeviceError::BackendUnavailable);
        }

        std::uint32_t count = 0;
        if (inst.vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0) {
            return std::unexpected(DeviceError::BackendUnavailable);
        }
        std::vector<VkPhysicalDevice> physical_devices(count);
        if (inst.vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()) != VK_SUCCESS) {
            return std::unexpected(DeviceError::BackendFailure);
        }
        struct AcceptableDevice {
            VkPhysicalDevice device = VK_NULL_HANDLE;
            std::uint32_t queue_family = 0;
            VkQueueFamilyProperties queue_properties{};
            VkPhysicalDeviceProperties properties{};
        };
        std::vector<AcceptableDevice> acceptable;
        for (VkPhysicalDevice candidate : physical_devices) {
            VkPhysicalDeviceVulkan12Features features12{};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceVulkan13Features features13{};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features13.pNext = &features12;
            VkPhysicalDeviceFeatures2 features{};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features13;
            VkPhysicalDeviceProperties candidate_properties{};
            inst.vkGetPhysicalDeviceProperties(candidate, &candidate_properties);
            inst.vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (candidate_properties.apiVersion < VK_API_VERSION_1_3 || !features13.dynamicRendering ||
                !features13.synchronization2 || !features12.timelineSemaphore || !features12.bufferDeviceAddress ||
                !features12.runtimeDescriptorArray || !features12.descriptorBindingPartiallyBound ||
                !features12.descriptorBindingSampledImageUpdateAfterBind ||
                !features12.descriptorBindingStorageBufferUpdateAfterBind ||
                !features12.descriptorBindingStorageImageUpdateAfterBind) {
                continue;
            }
            if (surface) {
                std::uint32_t device_extension_count = 0;
                if (inst.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &device_extension_count, nullptr) !=
                    VK_SUCCESS) {
                    continue;
                }
                std::vector<VkExtensionProperties> device_extensions(device_extension_count);
                if (inst.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &device_extension_count,
                                                              device_extensions.data()) != VK_SUCCESS ||
                    std::none_of(device_extensions.begin(), device_extensions.end(), [](const auto &extension) {
                        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                    })) {
                    continue;
                }
            }
            std::uint32_t family_count = 0;
            inst.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            inst.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
            for (std::uint32_t family = 0; family < family_count; ++family) {
                VkBool32 present = VK_TRUE;
                if (surface &&
                    inst.vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &present) != VK_SUCCESS) {
                    continue;
                }
                if (present && families[family].queueCount != 0 &&
                    (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                    acceptable.push_back(AcceptableDevice{candidate, family, families[family], candidate_properties});
                    break;
                }
            }
        }
        std::vector<VulkanDeviceType> device_types;
        device_types.reserve(acceptable.size());
        for (const AcceptableDevice &entry : acceptable) {
            device_types.push_back(static_cast<VulkanDeviceType>(entry.properties.deviceType));
        }
        const auto selected = selectPreferredDeviceIndex(device_types);
        if (!selected) {
            return std::unexpected(DeviceError::BackendUnavailable);
        }
        physicalDevice = acceptable[*selected].device;
        queueFamily = acceptable[*selected].queue_family;
        queueProperties = acceptable[*selected].queue_properties;
        properties = acceptable[*selected].properties;

        constexpr float priority = 1.0F;
        VkDeviceQueueCreateInfo queue_create{};
        queue_create.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create.queueFamilyIndex = queueFamily;
        queue_create.queueCount = 1;
        queue_create.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan12Features enable12{};
        enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enable12.timelineSemaphore = VK_TRUE;
        enable12.bufferDeviceAddress = VK_TRUE;
        enable12.runtimeDescriptorArray = VK_TRUE;
        enable12.descriptorBindingPartiallyBound = VK_TRUE;
        enable12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        enable12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        enable12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        VkPhysicalDeviceVulkan13Features enable13{};
        enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enable13.pNext = &enable12;
        enable13.dynamicRendering = VK_TRUE;
        enable13.synchronization2 = VK_TRUE;
        constexpr const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo device_create{};
        device_create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create.pNext = &enable13;
        device_create.queueCreateInfoCount = 1;
        device_create.pQueueCreateInfos = &queue_create;
        if (surface) {
            device_create.enabledExtensionCount = 1;
            device_create.ppEnabledExtensionNames = &swapchain_extension;
        }
        if (const VkResult result = inst.vkCreateDevice(physicalDevice, &device_create, nullptr, &device);
            result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        volkLoadInstanceOnly(instance);
        volkLoadDeviceTable(&vk, device);
        vk.vkGetDeviceQueue(device, queueFamily, 0, &queue);

        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = inst.vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocator_create{};
        allocator_create.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocator_create.instance = instance;
        allocator_create.physicalDevice = physicalDevice;
        allocator_create.device = device;
        allocator_create.vulkanApiVersion = VK_API_VERSION_1_3;
        allocator_create.pVulkanFunctions = &functions;
        if (const VkResult result = vmaCreateAllocator(&allocator_create, &allocator); result != VK_SUCCESS) {
            return std::unexpected(vulkanError(result));
        }
        return {};
    } catch (const std::bad_alloc &) {
        return std::unexpected(DeviceError::OutOfMemory);
    }
}

void VulkanRuntime::nameObject(VkObjectType type, std::uint64_t handle, const char *name) const noexcept {
    if (inst.vkSetDebugUtilsObjectNameEXT && *name) {
        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;
        inst.vkSetDebugUtilsObjectNameEXT(device, &info);
    }
}

void VulkanRuntime::beginLabel(VkCommandBuffer commands, const char *name) const noexcept {
    if (inst.vkCmdBeginDebugUtilsLabelEXT) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        inst.vkCmdBeginDebugUtilsLabelEXT(commands, &label);
    }
}

void VulkanRuntime::endLabel(VkCommandBuffer commands) const noexcept {
    if (inst.vkCmdEndDebugUtilsLabelEXT) {
        inst.vkCmdEndDebugUtilsLabelEXT(commands);
    }
}

DeviceError vulkanError(VkResult result) noexcept {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return DeviceError::OutOfMemory;
    case VK_ERROR_EXTENSION_NOT_PRESENT:
    case VK_ERROR_FEATURE_NOT_PRESENT:
    case VK_ERROR_INCOMPATIBLE_DRIVER:
    case VK_ERROR_LAYER_NOT_PRESENT:
        return DeviceError::BackendUnavailable;
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return DeviceError::Unsupported;
    default:
        return DeviceError::BackendFailure;
    }
}

VkFormat vulkanFormat(TextureFormat format) noexcept {
    switch (format) {
    case TextureFormat::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case TextureFormat::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Bgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::D24UnormS8Uint:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkImageUsageFlags vulkanTextureUsage(TextureUsage usage) noexcept {
    constexpr std::array uses{TextureUsage::Sampled,      TextureUsage::Storage,    TextureUsage::RenderTarget,
                              TextureUsage::DepthStencil, TextureUsage::CopySource, TextureUsage::CopyDestination};
    constexpr std::array<VkImageUsageFlags, uses.size()> flags{
        VK_IMAGE_USAGE_SAMPLED_BIT,          VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,     VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkImageUsageFlags result = 0;
    for (std::size_t index = 0; index < uses.size(); ++index) {
        if ((static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(uses[index])) != 0) {
            result |= flags[index];
        }
    }
    return result;
}

VmaAllocationCreateInfo vulkanAllocationInfo(MemoryType memory) noexcept {
    VmaAllocationCreateInfo info{};
    info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (memory == MemoryType::Upload) {
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    } else if (memory == MemoryType::Readback) {
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    return info;
}

VulkanTextureSync vulkanTextureSync(TextureState state) noexcept {
    constexpr VkPipelineStageFlags2 shaders = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    switch (state) {
    case TextureState::Sampled:
        return {shaders, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case TextureState::Storage:
        return {shaders, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case TextureState::ColorAttachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case TextureState::DepthAttachment:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case TextureState::CopySource:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case TextureState::CopyDestination:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case TextureState::Undefined:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
}

void transitionVulkanImage(const VulkanRuntime &runtime, VkCommandBuffer commands, VkImage image,
                           const VkImageSubresourceRange &range, VulkanTextureSync before,
                           VulkanTextureSync after) noexcept {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = before.stages;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stages;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = before.layout;
    barrier.newLayout = after.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    runtime.vk.vkCmdPipelineBarrier2(commands, &dependency);
}

} // namespace star::rhi
