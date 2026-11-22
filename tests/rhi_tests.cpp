#include "test_support.hpp"

#include "rhi/rhi.hpp"
#include "rhi/vulkan.hpp"

#include <array>
#include <memory>

namespace {

STAR_TEST(nullDeviceReportsCapabilitiesAndRejectsInvalidDescriptors) {
    const auto invalid = star::rhi::createNullDevice(star::rhi::DeviceCreateInfo{.framesInFlight = 0});
    STAR_CHECK(!invalid && invalid.error() == star::rhi::DeviceError::InvalidDescriptor);

    const auto device_result = star::rhi::createNullDevice();
    STAR_CHECK(device_result);
    const std::unique_ptr<star::rhi::Device> &device = *device_result;
    STAR_CHECK(device->getCapabilities().supportsComputeQueue);
    STAR_CHECK(device->getCapabilities().supportsDebugMarkers);

    const auto invalid_buffer = device->createBuffer(star::rhi::BufferDesc{});
    STAR_CHECK(!invalid_buffer && invalid_buffer.error() == star::rhi::DeviceError::InvalidDescriptor);
}

STAR_TEST(nullDeviceTracksResourcesAndReclaimsAfterFrameBoundary) {
    auto device_result = star::rhi::createNullDevice();
    STAR_CHECK(device_result);
    std::unique_ptr<star::rhi::Device> device = std::move(*device_result);

    const auto buffer_result = device->createBuffer(star::rhi::BufferDesc{
        .size = 256,
        .usage = star::rhi::BufferUsage::Vertex | star::rhi::BufferUsage::CopyDestination,
        .memory = star::rhi::MemoryType::GpuOnly,
        .debugName = "triangle-vertices",
    });
    const auto texture_result = device->createTexture(star::rhi::TextureDesc{
        .width = 4,
        .height = 4,
        .usage = star::rhi::TextureUsage::Sampled | star::rhi::TextureUsage::CopyDestination,
        .debugName = "checkerboard",
    });
    STAR_CHECK(buffer_result && texture_result);
    STAR_CHECK(device->isAlive(*buffer_result));
    STAR_CHECK(device->isAlive(*texture_result));

    STAR_CHECK(device->destroy(*buffer_result));
    STAR_CHECK(device->destroy(*texture_result));
    STAR_CHECK(device->isAlive(*buffer_result));
    STAR_CHECK(device->isAlive(*texture_result));

    device->beginFrame();
    STAR_CHECK(!device->isAlive(*buffer_result));
    STAR_CHECK(!device->isAlive(*texture_result));
    STAR_CHECK(!device->destroy(*buffer_result));
}

STAR_TEST(nullDeviceSubmitsEachCommandListOnce) {
    auto device_result = star::rhi::createNullDevice();
    STAR_CHECK(device_result);
    std::unique_ptr<star::rhi::Device> device = std::move(*device_result);

    const auto command_result = device->beginCommandList(star::rhi::QueueType::Graphics);
    STAR_CHECK(command_result);
    star::rhi::CommandList *command_list = *command_result;
    const std::array<star::rhi::BufferBarrier, 0> no_buffers{};
    const std::array<star::rhi::TextureBarrier, 0> no_textures{};
    command_list->barrier(no_buffers, no_textures);
    command_list->beginDebugMarker("clear");
    command_list->writeTimestamp(0);
    command_list->endDebugMarker();

    const std::array<star::rhi::CommandList *, 1> command_lists{command_list};
    const auto submit_result = device->submit(star::rhi::QueueType::Graphics, command_lists, {});
    STAR_CHECK(submit_result && submit_result->value == 1);
    const auto second_submit = device->submit(star::rhi::QueueType::Graphics, command_lists, {});
    STAR_CHECK(!second_submit && second_submit.error() == star::rhi::DeviceError::InvalidState);
}

STAR_TEST(vulkanProbeReportsLoaderOrRuntimeAvailability) {
    const auto probe = star::rhi::probeVulkan();
    if (!probe) {
        STAR_CHECK(probe.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   probe.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   probe.error() == star::rhi::VulkanProbeError::PhysicalDeviceEnumerationFailed);
        return;
    }
    STAR_CHECK(probe->apiVersion >= (1U << 22U));
}

STAR_TEST(vulkanContextCreatesLogicalDeviceWhenRuntimeIsAvailable) {
    auto context = star::rhi::createVulkanContext();
    if (!context) {
        STAR_CHECK(context.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   context.error() == star::rhi::VulkanProbeError::PhysicalDeviceUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::QueueFamilyUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::DeviceCreationFailed);
        return;
    }
    STAR_CHECK(context->isValid());
    STAR_CHECK(context->getInfo().physicalDeviceCount > 0);
}

STAR_TEST(vulkanContextSubmitsAndWaitsForEmptyCommandBuffer) {
    auto context = star::rhi::createVulkanContext();
    if (!context) {
        STAR_CHECK(context.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   context.error() == star::rhi::VulkanProbeError::PhysicalDeviceUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::QueueFamilyUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::DeviceCreationFailed);
        return;
    }
    const auto submit = context->submitEmptyCommandBuffer();
    STAR_CHECK(submit);
}

STAR_TEST(vulkanContextAllocatesAndReleasesTransientBufferMemory) {
    auto context = star::rhi::createVulkanContext();
    if (!context) {
        STAR_CHECK(context.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   context.error() == star::rhi::VulkanProbeError::PhysicalDeviceUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::QueueFamilyUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::DeviceCreationFailed);
        return;
    }
    STAR_CHECK(context->createTransientBuffer(256));
}

} // namespace
