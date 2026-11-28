#include "test_support.hpp"

#include "rhi/render_graph.hpp"
#include "rhi/rhi.hpp"
#include "rhi/shader_compiler.hpp"
#include "rhi/vulkan.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

STAR_TEST(nullDeviceValidatesDrawResourcesAndReportsInvalidBindings) {
    using namespace star::rhi;
    auto device_result = createNullDevice();
    STAR_CHECK(device_result);
    std::unique_ptr<Device> device = std::move(*device_result);

    const auto texture = device->createTexture({.width = 4,
                                                .height = 4,
                                                .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                                                .debugName = "font-atlas"});
    const auto upload = device->createBuffer(
        {.size = 64, .usage = BufferUsage::Vertex, .memory = MemoryType::Upload, .debugName = "vertices"});
    STAR_CHECK(texture && upload);
    const auto view = device->createTextureView({.texture = *texture, .debugName = "font-atlas-view"});
    const auto sampler = device->createSampler({.debugName = "linear-clamp"});
    STAR_CHECK(view && sampler);
    STAR_CHECK(!device->createTextureView({.texture = TextureHandle{}, .debugName = "bad"}));

    const std::array binding{BindGroupLayoutEntry{0, BindingType::CombinedImageSampler, ShaderStage::Fragment}};
    const auto layout = device->createBindGroupLayout({.entries = binding, .debugName = "ui-layout"});
    STAR_CHECK(layout);
    const std::array good_entry{BindGroupEntry{0, *view, *sampler, {}, 0, 0}};
    STAR_CHECK(device->createBindGroup({.layout = *layout, .entries = good_entry, .debugName = "ui-group"}));
    const std::array missing_sampler{BindGroupEntry{0, *view, SamplerHandle{}, {}, 0, 0}};
    STAR_CHECK(!device->createBindGroup({.layout = *layout, .entries = missing_sampler, .debugName = "bad-group"}));

    const std::array no_groups{*layout};
    const auto pipeline_layout = device->createPipelineLayout(
        {.bind_groups = no_groups, .push_constant_size = 16, .debugName = "ui-pipeline-layout"});
    STAR_CHECK(pipeline_layout);
    const auto vertex = device->createShaderModule(
        {.stage = ShaderStage::Vertex, .spirv = std::array<std::uint32_t, 1>{0x07230203U}, .debugName = "vs"});
    const auto fragment = device->createShaderModule(
        {.stage = ShaderStage::Fragment, .spirv = std::array<std::uint32_t, 1>{0x07230203U}, .debugName = "fs"});
    STAR_CHECK(vertex && fragment);
    const std::array formats{TextureFormat::Bgra8Unorm};
    STAR_CHECK(device->createGraphicsPipeline({.layout = *pipeline_layout,
                                               .vertex_shader = *vertex,
                                               .fragment_shader = *fragment,
                                               .vertex_buffers = {},
                                               .blend = BlendMode::Alpha,
                                               .color_formats = formats,
                                               .debugName = "ui-pipeline"}));
    STAR_CHECK(!device->createGraphicsPipeline({.layout = *pipeline_layout,
                                                .vertex_shader = *vertex,
                                                .fragment_shader = ShaderModuleHandle{},
                                                .vertex_buffers = {},
                                                .color_formats = formats,
                                                .debugName = "bad-pipeline"}));

    STAR_CHECK(!device->createSwapchain({.width = 16, .height = 16, .vsync = true, .debugName = "null-swapchain"})
                    .has_value());
    STAR_CHECK(!device->acquireNextImage(SwapchainHandle{}).has_value());
    const std::array<std::byte, 16> payload{};
    STAR_CHECK(device->writeBuffer(*upload, payload));
    STAR_CHECK(!device->readBuffer(*upload, std::span<std::byte>{}));
    STAR_CHECK(device->waitIdle());
}

STAR_TEST(vulkanDeviceClearsTextureAndReadsBackPixels) {
    using namespace star::rhi;
    const bool validation = enableVulkanValidation();
    auto device = createVulkanDevice({.enableValidation = validation});
    if (!device) {
        STAR_CHECK(device.error() == DeviceError::BackendUnavailable && !validation && !requireVulkan());
        return;
    }
    const auto texture = (*device)->createTexture({
        .width = 4,
        .height = 4,
        .format = TextureFormat::Rgba8Unorm,
        .usage = TextureUsage::RenderTarget | TextureUsage::Sampled | TextureUsage::CopySource,
        .debugName = "rhi-clear-target",
    });
    const auto readback = (*device)->createBuffer({
        .size = 4 * 4 * 4,
        .usage = BufferUsage::CopyDestination,
        .memory = MemoryType::Readback,
        .debugName = "rhi-clear-readback",
    });
    STAR_CHECK(texture && readback);
    auto commands = (*device)->beginCommandList(QueueType::Graphics);
    STAR_CHECK(commands);
    (*commands)->beginRenderPass(RenderPassBeginInfo{
        .color_target = *texture,
        .clear_color = {0.25F, 0.5F, 0.75F, 1.0F},
        .clear = true,
        .debugName = "clear pass",
    });
    (*commands)->endRenderPass();
    (*commands)->copyTextureToBuffer(*texture, *readback);
    const std::array list{*commands};
    STAR_CHECK((*device)->submit(QueueType::Graphics, list, {}));

    std::array<std::byte, 64> pixels{};
    STAR_CHECK((*device)->readBuffer(*readback, pixels));
    const auto byte_at = [&pixels](std::size_t offset) { return std::to_integer<int>(pixels[offset]); };
    STAR_CHECK(std::abs(byte_at(0) - 64) <= 2);
    STAR_CHECK(std::abs(byte_at(1) - 128) <= 2);
    STAR_CHECK(std::abs(byte_at(2) - 191) <= 2);
    STAR_CHECK(byte_at(3) == 255);

    STAR_CHECK((*device)->destroy(*texture) && (*device)->destroy(*readback));
    (*device)->beginFrame();
    STAR_CHECK(!(*device)->isAlive(*texture));
}

STAR_TEST(vulkanDeviceRoundTripsUploadedBufferBytes) {
    using namespace star::rhi;
    auto device = createVulkanDevice({.enableValidation = enableVulkanValidation()});
    if (!device) {
        STAR_CHECK(device.error() == DeviceError::BackendUnavailable && !requireVulkan());
        return;
    }
    constexpr std::uint64_t byte_count = 40;
    auto upload = (*device)->createBuffer({.size = byte_count,
                                           .usage = BufferUsage::CopySource,
                                           .memory = MemoryType::Upload,
                                           .debugName = "rhi-roundtrip-upload"});
    auto readback = (*device)->createBuffer({.size = byte_count,
                                             .usage = BufferUsage::CopyDestination,
                                             .memory = MemoryType::Readback,
                                             .debugName = "rhi-roundtrip-readback"});
    STAR_CHECK(upload && readback);
    std::array<std::byte, byte_count> source{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::byte>((index + 1) & 0xFFU);
    }
    STAR_CHECK((*device)->writeBuffer(*upload, source));
    auto commands = (*device)->beginCommandList(QueueType::Graphics);
    STAR_CHECK(commands);
    (*commands)->copyBuffer(*upload, *readback, byte_count);
    const std::array list{*commands};
    STAR_CHECK((*device)->submit(QueueType::Graphics, list, {}));
    std::array<std::byte, byte_count> result{};
    STAR_CHECK((*device)->readBuffer(*readback, result));
    STAR_CHECK(result == source);
    STAR_CHECK((*device)->destroy(*upload) && (*device)->destroy(*readback));
}

STAR_TEST(vulkanDeviceCreatesSampledTextureBindings) {
    using namespace star::rhi;
    auto device = createVulkanDevice({.enableValidation = enableVulkanValidation()});
    if (!device) {
        STAR_CHECK(device.error() == DeviceError::BackendUnavailable && !requireVulkan());
        return;
    }
    const auto texture = (*device)->createTexture({.width = 2,
                                                   .height = 2,
                                                   .format = TextureFormat::Rgba8Unorm,
                                                   .usage = TextureUsage::Sampled | TextureUsage::CopyDestination,
                                                   .debugName = "rhi-bind-texture"});
    STAR_CHECK(texture);
    const auto view = (*device)->createTextureView({.texture = *texture, .debugName = "rhi-bind-view"});
    const auto sampler = (*device)->createSampler(
        {.min_filter = FilterMode::Nearest, .mag_filter = FilterMode::Nearest, .debugName = "rhi-bind-sampler"});
    STAR_CHECK(view && sampler);
    const std::array binding{BindGroupLayoutEntry{0, BindingType::CombinedImageSampler, ShaderStage::Fragment}};
    const auto layout = (*device)->createBindGroupLayout({.entries = binding, .debugName = "rhi-bind-layout"});
    STAR_CHECK(layout);
    const std::array entry{BindGroupEntry{0, *view, *sampler, {}, 0, 0}};
    const auto group = (*device)->createBindGroup({.layout = *layout, .entries = entry, .debugName = "rhi-bind-group"});
    STAR_CHECK(group);
    STAR_CHECK((*device)->destroy(*group) && (*device)->destroy(*layout) && (*device)->destroy(*view) &&
               (*device)->destroy(*sampler) && (*device)->destroy(*texture));
}

} // namespace
