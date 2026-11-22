#pragma once

#include "rhi/export.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>

namespace star::rhi {

enum class VulkanProbeError : std::uint8_t {
    LoaderUnavailable,
    MissingEntryPoint,
    InstanceCreationFailed,
    PhysicalDeviceEnumerationFailed,
    ValidationLayerUnavailable,
    PhysicalDeviceUnavailable,
    QueueFamilyUnavailable,
    DeviceCreationFailed,
    CommandCreationFailed,
    SubmissionFailed,
    ResourceCreationFailed,
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

struct VulkanDeviceInfo {
    std::uint32_t apiVersion = 0;
    std::uint32_t physicalDeviceCount = 0;
    std::uint32_t graphicsQueueFamily = 0;
    std::uint32_t computeQueueFamily = 0;
    std::uint32_t copyQueueFamily = 0;
};

class STAR_RHI_API VulkanContext final {
  public:
    ~VulkanContext();
    VulkanContext(VulkanContext &&other) noexcept;
    VulkanContext &operator=(VulkanContext &&other) noexcept;
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    [[nodiscard]] const VulkanDeviceInfo &getInfo() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] std::expected<void, VulkanProbeError> submitEmptyCommandBuffer() noexcept;
    [[nodiscard]] std::expected<void, VulkanProbeError> createTransientBuffer(std::uint64_t size) noexcept;

  private:
    struct Impl;
    explicit VulkanContext(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;

    friend STAR_RHI_API std::expected<VulkanContext, VulkanProbeError>
    createVulkanContext(const VulkanProbeOptions &options);
};

[[nodiscard]] STAR_RHI_API std::expected<VulkanProbeInfo, VulkanProbeError>
probeVulkan(const VulkanProbeOptions &options = {});

[[nodiscard]] STAR_RHI_API std::expected<VulkanContext, VulkanProbeError>
createVulkanContext(const VulkanProbeOptions &options = {});

} // namespace star::rhi
