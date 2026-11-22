#pragma once

#include "rhi/export.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace star::rhi {

enum class VulkanProbeError : std::uint8_t {
    LoaderUnavailable,
    MissingEntryPoint,
    InstanceCreationFailed,
    PhysicalDeviceEnumerationFailed,
    ValidationLayerUnavailable,
    OutOfMemory,
};

struct VulkanProbeOptions {
    std::string_view applicationName = "StarEngine";
    std::string_view engineName = "StarEngine";
    bool requireValidationLayer = false;
};

struct VulkanProbeInfo {
    std::uint32_t apiVersion = 0;
    std::uint32_t physicalDeviceCount = 0;
    bool validationLayerAvailable = false;
};

[[nodiscard]] STAR_RHI_API std::expected<VulkanProbeInfo, VulkanProbeError>
probeVulkan(const VulkanProbeOptions &options = {});

} // namespace star::rhi
