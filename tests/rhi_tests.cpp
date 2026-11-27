#include "test_support.hpp"

#include "rhi/render_graph.hpp"
#include "rhi/rhi.hpp"
#include "rhi/shader_compiler.hpp"
#include "rhi/vulkan.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <span>

namespace {

[[nodiscard]] bool requireVulkan() { return std::getenv("STAR_TEST_REQUIRE_VULKAN") != nullptr; }

[[nodiscard]] bool enableVulkanValidation() { return std::getenv("STAR_TEST_VULKAN_VALIDATION") != nullptr; }

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

STAR_TEST(deviceSelectionPrefersDiscreteGpu) {
    const std::array types{star::rhi::VulkanDeviceType::Integrated, star::rhi::VulkanDeviceType::Discrete,
                           star::rhi::VulkanDeviceType::Cpu};
    const auto selected = star::rhi::selectPreferredDevice(types);
    STAR_CHECK(selected.has_value() && *selected == 1);
}

STAR_TEST(deviceSelectionKeepsEnumerationOrderForEqualTypes) {
    const std::array types{star::rhi::VulkanDeviceType::Integrated, star::rhi::VulkanDeviceType::Integrated};
    const auto selected = star::rhi::selectPreferredDevice(types);
    STAR_CHECK(selected.has_value() && *selected == 0);
}

STAR_TEST(deviceSelectionFallsBackToLowerRankedDevices) {
    const std::array types{star::rhi::VulkanDeviceType::Cpu, star::rhi::VulkanDeviceType::Virtual};
    const auto selected = star::rhi::selectPreferredDevice(types);
    STAR_CHECK(selected.has_value() && *selected == 1);
}

STAR_TEST(deviceSelectionRejectsEmptyDeviceList) {
    STAR_CHECK(!star::rhi::selectPreferredDevice(std::span<const star::rhi::VulkanDeviceType>{}));
}

STAR_TEST(vulkanProbeReportsLoaderOrRuntimeAvailability) {
    const auto probe = star::rhi::probeVulkan({.requireValidationLayer = enableVulkanValidation()});
    if (!probe) {
        STAR_CHECK(!requireVulkan());
        STAR_CHECK(probe.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   probe.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   probe.error() == star::rhi::VulkanProbeError::PhysicalDeviceEnumerationFailed);
        return;
    }
    STAR_CHECK(probe->apiVersion >= (1U << 22U));
    if (requireVulkan()) {
        STAR_CHECK(probe->physicalDeviceCount > 0);
    }
}

STAR_TEST(vulkanContextCreatesLogicalDeviceWhenRuntimeIsAvailable) {
    auto context = star::rhi::createVulkanContext({.requireValidationLayer = enableVulkanValidation()});
    if (!context) {
        STAR_CHECK(!requireVulkan());
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
    auto context = star::rhi::createVulkanContext({.requireValidationLayer = enableVulkanValidation()});
    if (!context) {
        STAR_CHECK(!requireVulkan());
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
    auto context = star::rhi::createVulkanContext({.requireValidationLayer = enableVulkanValidation()});
    if (!context) {
        STAR_CHECK(!requireVulkan());
        STAR_CHECK(context.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   context.error() == star::rhi::VulkanProbeError::PhysicalDeviceUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::QueueFamilyUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::DeviceCreationFailed);
        return;
    }
    STAR_CHECK(context->createTransientBuffer(256));
}

STAR_TEST(vulkanContextAllocatesAndReleasesTransientTextureMemory) {
    auto context = star::rhi::createVulkanContext({.requireValidationLayer = enableVulkanValidation()});
    if (!context) {
        STAR_CHECK(!requireVulkan());
        STAR_CHECK(context.error() == star::rhi::VulkanProbeError::LoaderUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::InstanceCreationFailed ||
                   context.error() == star::rhi::VulkanProbeError::PhysicalDeviceUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::QueueFamilyUnavailable ||
                   context.error() == star::rhi::VulkanProbeError::DeviceCreationFailed);
        return;
    }
    STAR_CHECK(context->createTransientTexture(4, 4));
}

STAR_TEST(vulkanDeviceSubmitsResourceTransitionsAndRetiresHandles) {
    using namespace star::rhi;
    const bool validation = enableVulkanValidation();
    auto device = createVulkanDevice({.enableValidation = validation});
    if (!device) {
        STAR_CHECK(device.error() == DeviceError::BackendUnavailable && !validation && !requireVulkan());
        return;
    }
    STAR_CHECK(!validation || (*device)->getCapabilities().supportsValidation);
    const auto buffer = (*device)->createBuffer(
        {.size = 64, .usage = BufferUsage::CopySource, .memory = MemoryType::Upload, .debugName = "rhi-test-buffer"});
    const auto texture = (*device)->createTexture({.width = 4,
                                                   .height = 4,
                                                   .usage = TextureUsage::CopyDestination | TextureUsage::Sampled,
                                                   .debugName = "rhi-test-texture"});
    STAR_CHECK(buffer && texture);
    auto commands = (*device)->beginCommandList(QueueType::Graphics);
    STAR_CHECK(commands);
    (*commands)->beginDebugMarker("RHI texture transitions");
    (*commands)->writeTimestamp(0);
    const std::array prepare{TextureBarrier{*texture, TextureState::Undefined, TextureState::CopyDestination}};
    const std::array sample{TextureBarrier{*texture, TextureState::CopyDestination, TextureState::Sampled}};
    (*commands)->barrier({}, prepare);
    (*commands)->barrier({}, sample);
    (*commands)->writeTimestamp(1);
    (*commands)->endDebugMarker();
    const std::array list{*commands};
    STAR_CHECK((*device)->submit(QueueType::Graphics, list, {}));
    STAR_CHECK(!(*device)->submit(QueueType::Graphics, list, {}));
    if ((*device)->getCapabilities().supportsTimestampQueries) {
        const auto timestamp = (*commands)->readTimestampNanoseconds(1);
        STAR_CHECK(timestamp && std::isfinite(*timestamp) && *timestamp >= 0.0);
    }
    STAR_CHECK((*device)->destroy(*buffer) && (*device)->destroy(*texture));
    (*device)->beginFrame();
    STAR_CHECK(!(*device)->isAlive(*buffer) && !(*device)->isAlive(*texture));
    STAR_CHECK(!(*device)->destroy(*texture));
}

STAR_TEST(shaderCompilerProducesValidatedSpirvWhenToolsAreAvailable) {
    constexpr std::string_view source = R"(
#version 450
void main() {
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";
    const auto module = star::rhi::compileGlslToSpirv(source);
    if (!module) {
        STAR_CHECK(module.error() == star::rhi::ShaderCompileError::CompilerUnavailable ||
                   module.error() == star::rhi::ShaderCompileError::CompileFailed ||
                   module.error() == star::rhi::ShaderCompileError::ValidationFailed);
        return;
    }
    STAR_CHECK(!module->words.empty());
    STAR_CHECK(module->words.front() == 0x07230203U);
}

STAR_TEST(renderGraphOrdersDependenciesAndCullsUnusedPasses) {
    star::rhi::RenderGraph graph;
    const auto input = graph.createTexture("Input", {.width = 4, .height = 4});
    const auto output = graph.createTexture("Output", {.width = 4, .height = 4});
    const auto unused = graph.createTexture("Unused", {.width = 4, .height = 4});
    STAR_CHECK(input && output && unused);

    const std::array<star::rhi::RenderResourceUse, 1> write_input{{{*input, true}}};
    const std::array<star::rhi::RenderResourceUse, 2> read_write_output{{{*input, false}, {*output, true}}};
    const std::array<star::rhi::RenderResourceUse, 1> write_unused{{{*unused, true}}};
    STAR_CHECK(graph.addPass({"Prepare", star::rhi::RenderPassType::Raster, write_input}));
    STAR_CHECK(graph.addPass({"Composite", star::rhi::RenderPassType::Raster, read_write_output}));
    STAR_CHECK(graph.addPass({"Unused", star::rhi::RenderPassType::Compute, write_unused}));

    const std::array<star::rhi::RenderTextureHandle, 1> outputs{*output};
    const auto compiled = graph.compile(outputs);
    STAR_CHECK(compiled);
    STAR_CHECK(compiled->passOrder.size() == 2);
    STAR_CHECK(compiled->passOrder[0].id == 0 && compiled->passOrder[1].id == 1);
    STAR_CHECK(compiled->barriers.size() == 1);
    STAR_CHECK(compiled->lifetimes.size() == 2);
}

STAR_TEST(renderGraphReusesCompatibleTexturesAfterTheirLastPass) {
    using namespace star::rhi;
    RenderGraph graph;
    constexpr RenderTextureDesc transient{.width = 8, .height = 8, .usage = TextureUsage::RenderTarget};
    auto imported_desc = transient;
    imported_desc.imported = true;
    const auto first = graph.createTexture("First", transient);
    const auto overlap = graph.createTexture("Overlapping", transient);
    const auto later = graph.createTexture("Later", transient);
    const auto imported = graph.createTexture("Imported", imported_desc);
    STAR_CHECK(first && overlap && later && imported);

    const std::array prepare{RenderResourceUse{*first, true}};
    const std::array use_first{RenderResourceUse{*first, false}, RenderResourceUse{*overlap, true}};
    const std::array use_later{RenderResourceUse{*later, true}, RenderResourceUse{*imported, true}};
    STAR_CHECK(graph.addPass({"Prepare", RenderPassType::Raster, prepare}));
    STAR_CHECK(graph.addPass({"Read", RenderPassType::Raster, use_first}));
    const auto final = graph.addPass({"Reuse", RenderPassType::Raster, use_later});
    STAR_CHECK(final);
    const auto graph_result = graph.compile();
    STAR_CHECK(graph_result && graph_result->transientTextures.size() == 2);
    STAR_CHECK(graph_result->lifetimes[first->id].transientSlot != graph_result->lifetimes[overlap->id].transientSlot);
    STAR_CHECK(graph_result->lifetimes[first->id].transientSlot == graph_result->lifetimes[later->id].transientSlot);
    STAR_CHECK(graph_result->lifetimes[imported->id].transientSlot == UINT32_MAX);
    STAR_CHECK(graph_result->textureReuses.size() == 1 && graph_result->textureReuses[0].previous == *first &&
               graph_result->textureReuses[0].next == *later && graph_result->textureReuses[0].beforePass == *final);
}

} // namespace
