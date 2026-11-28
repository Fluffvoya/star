#include "renderer/forward_renderer.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#ifdef STAR_FORWARD_VULKAN
#include "cluster_sizes.h"
#include "environment.hpp"
#include "vulkan_runtime.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#endif

namespace star::renderer {

#ifdef STAR_FORWARD_VULKAN
namespace {

using namespace star::rhi;
using star::math::Vector3;

constexpr VkExtent2D resolution{960, 640};
constexpr VkExtent2D shadow_resolution{1024, 1024};
constexpr VkFormat environment_format = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat color_format = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
constexpr std::size_t image_bytes = resolution.width * resolution.height * 4;

template <typename T> T vkInfo(VkStructureType type) {
    T info{};
    info.sType = type;
    return info;
}

void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " returned " + std::to_string(result));
    }
}

struct Buffer {
    explicit Buffer(const VulkanRuntime &runtime) : gpu(runtime) {}
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;

    const VulkanRuntime &gpu;
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    ~Buffer() {
        if (handle) {
            vmaDestroyBuffer(gpu.allocator, handle, allocation);
        }
    }

    void create(VkDeviceSize size, VkBufferUsageFlags flags, MemoryType memory) {
        auto info = vkInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        info.size = size;
        info.usage = flags;
        const auto allocation_info = vulkanAllocationInfo(memory);
        check(vmaCreateBuffer(gpu.allocator, &info, &allocation_info, &handle, &allocation, nullptr), "Create buffer");
    }

    void upload(std::span<const std::byte> bytes, VkBufferUsageFlags flags) {
        create(bytes.size(), flags, MemoryType::Upload);
        check(vmaCopyMemoryToAllocation(gpu.allocator, bytes.data(), allocation, 0, bytes.size()),
              "Write staging buffer");
    }
};

struct Image {
    explicit Image(const VulkanRuntime &runtime) : gpu(runtime) {}
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    const VulkanRuntime &gpu;
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageSubresourceRange range{0, 0, 1, 0, 1};

    ~Image() {
        if (view) {
            gpu.vk.vkDestroyImageView(gpu.device, view, nullptr);
        }
        if (handle) {
            vmaDestroyImage(gpu.allocator, handle, allocation);
        }
    }

    void create(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkExtent2D extent = resolution,
                std::uint32_t layers = 1, std::uint32_t mip_count = 1) {
        range.aspectMask = aspect;
        range.layerCount = layers;
        range.levelCount = mip_count;
        auto image = vkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {extent.width, extent.height, 1};
        image.mipLevels = mip_count;
        image.arrayLayers = layers;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = usage;
        const auto memory = vulkanAllocationInfo(MemoryType::GpuOnly);
        check(vmaCreateImage(gpu.allocator, &image, &memory, &handle, &allocation, nullptr), "Create image");
        auto info = vkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        info.image = handle;
        info.viewType = layers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        info.format = format;
        info.subresourceRange = range;
        check(gpu.vk.vkCreateImageView(gpu.device, &info, nullptr, &view), "Create image view");
    }
};

struct DrawConstants {
    std::array<float, 4> clip_x;
    std::array<float, 4> clip_y;
    std::array<float, 4> clip_z;
    std::array<float, 4> clip_w;
    std::array<float, 4> eye;
    std::array<float, 4> albedo;
    std::array<float, 4> factors;
    std::array<float, 4> cluster_params;
};

static_assert(sizeof(DrawConstants) == 128);
constexpr auto all_shader_stages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

struct PointLight {
    std::array<float, 4> position_radius;
    std::array<float, 4> color_intensity;
};

enum class DrawPass { Depth, Shadow, Color };

struct DrawPart {
    std::uint32_t index_count;
    std::uint32_t index_offset;
    std::int32_t vertex_offset;
    std::array<float, 4> albedo;
    std::array<float, 4> factors;
    VkDescriptorSet textures = VK_NULL_HANDLE;
    bool cast_shadows = true;
};

DrawConstants makeCamera(std::span<const StaticMeshVertex> vertices, bool front_view) {
    using namespace star::math;
    Vector3 low = vertices.front().position;
    Vector3 high = low;
    for (const auto &vertex : vertices) {
        const auto &p = vertex.position;
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const auto center = (low + high) * 0.5F;
    const float radius = std::max(length(high - low) * 0.5F, 1e-6F);
    const auto camera_direction = front_view ? Vector3{0, 0, 1} : normalize(Vector3{0.9F, 0.62F, 1.0F});
    const auto eye = center + camera_direction * (radius * 2.6F);
    const auto forward = normalize(center - eye);
    const auto right = normalize(cross(forward, {0.0F, 1.0F, 0.0F}));
    const auto up = cross(right, forward);
    constexpr float focal = 1.8F;
    constexpr float aspect = static_cast<float>(resolution.width) / resolution.height;
    const float near_plane = radius * 0.05F;
    const float far_plane = radius * 8.0F;
    const float scale_z = far_plane / (far_plane - near_plane);
    const auto row = [&](Vector3 direction) {
        return std::array{direction.x, direction.y, direction.z, -dot(direction, eye)};
    };
    DrawConstants constants{};
    constants.clip_x = row(right * (focal / aspect));
    constants.clip_y = row(up * -focal);
    constants.clip_w = row(forward);
    constants.clip_z = row(forward * scale_z);
    constants.clip_z[3] -= near_plane * scale_z;
    constants.eye = {eye.x, eye.y, eye.z, 1.0F};
    constants.cluster_params = {near_plane, far_plane, static_cast<float>(resolution.width),
                                static_cast<float>(resolution.height)};
    return constants;
}

struct ShadowView {
    std::array<float, 4> clip_x;
    std::array<float, 4> clip_y;
    std::array<float, 4> clip_z;
};

struct ShadowFrame {
    std::array<ShadowView, STAR_SHADOW_CASCADES> views;
    std::array<float, STAR_SHADOW_CASCADES> split_distances;
    std::array<float, 4> light_direction;
};
static_assert(sizeof(ShadowFrame) == 224);

ShadowFrame makeShadowFrame(const DrawConstants &camera) {
    using namespace star::math;
    const auto xyz = [](const std::array<float, 4> &value) { return Vector3{value[0], value[1], value[2]}; };
    const auto eye = xyz(camera.eye);
    const auto forward = xyz(camera.clip_w);
    const auto camera_right = xyz(camera.clip_x);
    const auto camera_down = xyz(camera.clip_y);
    const auto horizontal = camera_right / lengthSquared(camera_right);
    const auto vertical = camera_down / lengthSquared(camera_down);
    const float near_plane = camera.cluster_params[0];
    const float far_plane = camera.cluster_params[1];
    const float radius = near_plane / 0.05F;
    const auto scene_center = eye + forward * (radius * 2.6F);

    const Vector3 direction = normalize(Vector3{-0.45F, 0.85F, 0.35F});
    const Vector3 light_forward = -direction;
    const auto light_right = normalize(cross(light_forward, {0, 1, 0}));
    const auto light_down = cross(light_right, light_forward) * -1.0F;

    ShadowFrame result{};
    result.light_direction = {direction.x, direction.y, direction.z, 0};
    float split_near = near_plane;
    const auto ortho_row = [](Vector3 axis, float low, float high, float range_min, float range_size) {
        const float scale = range_size / (high - low);
        return std::array{axis.x * scale, axis.y * scale, axis.z * scale, range_min - low * scale};
    };
    for (std::size_t layer = 0; layer < result.views.size(); ++layer) {
        const auto fraction = static_cast<float>(layer + 1) / static_cast<float>(result.views.size());
        const auto logarithmic = near_plane * std::pow(far_plane / near_plane, fraction);
        const auto uniform = std::lerp(near_plane, far_plane, fraction);
        const auto split_far = std::lerp(uniform, logarithmic, 0.75F);
        float min_x = std::numeric_limits<float>::max();
        float max_x = -min_x;
        float min_y = min_x;
        float max_y = max_x;
        for (const float depth : {split_near, split_far}) {
            const auto center = eye + forward * depth;
            for (const float x : {-1.0F, 1.0F}) {
                for (const float y : {-1.0F, 1.0F}) {
                    const auto point = center + (horizontal * x + vertical * y) * depth;
                    const float light_x = dot(light_right, point);
                    const float light_y = dot(light_down, point);
                    min_x = std::min(min_x, light_x);
                    max_x = std::max(max_x, light_x);
                    min_y = std::min(min_y, light_y);
                    max_y = std::max(max_y, light_y);
                }
            }
        }
        const float margin = radius * 0.025F;
        const float shadow_center_z = dot(light_forward, scene_center);
        result.views[layer] = {
            ortho_row(light_right, min_x - margin, max_x + margin, -1, 2),
            ortho_row(light_down, min_y - margin, max_y + margin, -1, 2),
            ortho_row(light_forward, shadow_center_z - radius - margin, shadow_center_z + radius + margin, 0, 1)};
        result.split_distances[layer] = split_far;
        split_near = split_far;
    }
    return result;
}

class VulkanForwardPasses {
  public:
    explicit VulkanForwardPasses(const VulkanRuntime &gpu)
        : m_gpu(gpu), m_vertices{gpu}, m_indices{gpu}, m_capture{gpu}, m_lights{gpu}, m_cluster_lists{gpu},
          m_shadow_buffer{gpu}, m_environment_buffer{gpu}, m_color{gpu}, m_depth{gpu}, m_shadows{gpu},
          m_environment{gpu} {}

    ~VulkanForwardPasses() {
        if (!m_gpu.device) {
            return;
        }
        const auto &vk = m_gpu.vk;
        const auto device = m_gpu.device;
        vk.vkDeviceWaitIdle(device);
        vk.vkDestroyPipeline(device, m_prepass, nullptr);
        vk.vkDestroyPipeline(device, m_forward, nullptr);
        vk.vkDestroyPipeline(device, m_shadow_pipeline, nullptr);
        for (const auto layer : m_shadow_layers) {
            vk.vkDestroyImageView(device, layer, nullptr);
        }
        vk.vkDestroyPipeline(device, m_cluster_pipeline, nullptr);
        vk.vkDestroyPipelineLayout(device, m_layout, nullptr);
        vk.vkDestroyDescriptorPool(device, m_descriptor_pool, nullptr);
        vk.vkDestroyDescriptorPool(device, m_material_pool, nullptr);
        vk.vkDestroyDescriptorSetLayout(device, m_material_layout, nullptr);
        vk.vkDestroySampler(device, m_material_sampler, nullptr);
        vk.vkDestroyDescriptorSetLayout(device, m_descriptor_layout, nullptr);
        vk.vkDestroySampler(device, m_environment_sampler, nullptr);
        vk.vkDestroyQueryPool(device, m_queries, nullptr);
        vk.vkDestroyFence(device, m_fence, nullptr);
        vk.vkDestroyCommandPool(device, m_pool, nullptr);
    }

    void initialize(const StaticModelAsset &model, const ForwardRenderOptions &options,
                    const ForwardMaterial &material);
    double draw();
    ForwardImage capture() const;

  private:
    VkPipeline createPipeline(VkShaderModule vertex, VkShaderModule fragment, VkExtent2D extent,
                              bool double_sided) const;
    VkShaderModule loadShader(const std::filesystem::path &path) const;
    void renderPass(bool depth_only) const;
    void createLightingDescriptors();
    void createShadows();
    void createEnvironment();
    void createMaterialDescriptors(const StaticModelAsset &model);
    VkBuffer stageUpload(std::span<const std::byte> bytes);
    void uploadBuffer(Buffer &buffer, std::span<const std::byte> bytes, VkBufferUsageFlags usage,
                      VkPipelineStageFlags2 stages, VkAccessFlags2 access);
    void uploadImage(const Image &image, std::span<const std::byte> bytes, std::span<const VkBufferImageCopy> regions);
    void submitCommands() const;
    void renderShadows() const;
    void drawParts(const DrawConstants &frame, DrawPass pass) const;
    void dispatchClusters() const;

    const VulkanRuntime &m_gpu;
    Buffer m_vertices;
    Buffer m_indices;
    Buffer m_capture;
    Buffer m_lights;
    Buffer m_cluster_lists;
    Buffer m_shadow_buffer;
    Buffer m_environment_buffer;
    Image m_color;
    Image m_depth;
    Image m_shadows;
    Image m_environment;
    VkSampler m_environment_sampler = VK_NULL_HANDLE;
    ShadowFrame m_shadow_frame{};
    std::array<VkImageView, STAR_SHADOW_CASCADES> m_shadow_layers{};
    std::vector<DrawPart> m_parts;
    std::vector<std::unique_ptr<Image>> m_material_images;
    VkDescriptorSetLayout m_material_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_material_pool = VK_NULL_HANDLE;
    VkSampler m_material_sampler = VK_NULL_HANDLE;
    DrawConstants m_camera{};
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptors = VK_NULL_HANDLE;
    VkPipeline m_cluster_pipeline = VK_NULL_HANDLE;
    VkPipeline m_shadow_pipeline = VK_NULL_HANDLE;
    VkPipeline m_prepass = VK_NULL_HANDLE;
    VkPipeline m_forward = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_commands = VK_NULL_HANDLE;
    VkCommandBuffer m_upload_commands = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> m_staging;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
};

void VulkanForwardPasses::initialize(const StaticModelAsset &model, const ForwardRenderOptions &options,
                                     const ForwardMaterial &material) {
    if ((m_gpu.queueProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) {
        throw std::runtime_error("The graphics queue must also support compute shaders");
    }
    std::vector<StaticMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    for (const auto &part : model.parts) {
        if (part.indices.size() > std::numeric_limits<std::uint32_t>::max() - indices.size() ||
            part.vertices.size() >
                static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - vertices.size()) {
            throw std::runtime_error("Model exceeds indexed drawing limits");
        }
        DrawPart draw{static_cast<std::uint32_t>(part.indices.size()),
                      static_cast<std::uint32_t>(indices.size()),
                      static_cast<std::int32_t>(vertices.size()),
                      {0.48F, 0.55F, 0.62F, 1},
                      {0.7F, 0.0F, 0, 0}};
        if (part.material) {
            draw.albedo = part.material->base_color;
            draw.factors = {part.material->roughness, part.material->metallic, 0, 0};
        }
        draw.albedo = material.base_color.value_or(draw.albedo);
        draw.factors[0] = material.roughness.value_or(draw.factors[0]);
        draw.factors[1] = material.metallic.value_or(draw.factors[1]);
        if (!options.shadow_casters.empty()) {
            draw.cast_shadows = options.shadow_casters[m_parts.size()] != 0;
        }
        m_parts.push_back(draw);
        vertices.insert(vertices.end(), part.vertices.begin(), part.vertices.end());
        indices.insert(indices.end(), part.indices.begin(), part.indices.end());
    }
    m_camera = makeCamera(vertices, options.front_view);
    m_color.create(color_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT);
    m_depth.create(depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_capture.create(image_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::Readback);

    const auto &vk = m_gpu.vk;
    const auto device = m_gpu.device;
    auto pool = vkInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool.queueFamilyIndex = m_gpu.queueFamily;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vk.vkCreateCommandPool(device, &pool, nullptr, &m_pool), "Create command pool");
    auto commands = vkInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    commands.commandPool = m_pool;
    std::array<VkCommandBuffer, 2> command_buffers{};
    commands.commandBufferCount = static_cast<std::uint32_t>(command_buffers.size());
    check(vk.vkAllocateCommandBuffers(device, &commands, command_buffers.data()), "Allocate command buffers");
    m_commands = command_buffers[0];
    m_upload_commands = command_buffers[1];
    const auto fence = vkInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    check(vk.vkCreateFence(device, &fence, nullptr, &m_fence), "Create submission fence");
    if (m_gpu.queueProperties.timestampValidBits != 0) {
        auto query = vkInfo<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query.queryCount = 2;
        check(vk.vkCreateQueryPool(device, &query, nullptr, &m_queries), "Create timestamp queries");
    }
    auto upload_begin = vkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    upload_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vk.vkBeginCommandBuffer(m_upload_commands, &upload_begin), "Begin static resource upload");
    uploadBuffer(m_vertices, std::as_bytes(std::span(vertices)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    uploadBuffer(m_indices, std::as_bytes(std::span(indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
    createShadows();
    createEnvironment();
    createLightingDescriptors();
    createMaterialDescriptors(model);
    const VkPushConstantRange constants{all_shader_stages, 0, sizeof(DrawConstants)};
    auto layout = vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    const std::array descriptor_layouts{m_descriptor_layout, m_material_layout};
    layout.setLayoutCount = static_cast<std::uint32_t>(descriptor_layouts.size());
    layout.pSetLayouts = descriptor_layouts.data();
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    check(vk.vkCreatePipelineLayout(device, &layout, nullptr, &m_layout), "Create pipeline layout");

    const auto destroy_shader = [&](VkShaderModule shader) { vk.vkDestroyShaderModule(device, shader, nullptr); };
    using Shader = std::unique_ptr<std::remove_pointer_t<VkShaderModule>, decltype(destroy_shader)>;
    const Shader vertex(loadShader(options.shader_directory / "forward-vertex.spv"), destroy_shader);
    const auto &state = material.variant.state;
    const auto fragment_file =
        state.shading == star::MaterialShadingModel::Unlit ? "forward-unlit.spv" : "forward-fragment.spv";
    const Shader fragment(loadShader(options.shader_directory / fragment_file), destroy_shader);
    m_prepass = createPipeline(vertex.get(), VK_NULL_HANDLE, resolution, state.double_sided);
    m_forward = createPipeline(vertex.get(), fragment.get(), resolution, state.double_sided);
    m_shadow_pipeline = createPipeline(vertex.get(), VK_NULL_HANDLE, shadow_resolution, state.double_sided);
    const Shader cluster(loadShader(options.shader_directory / "forward-cluster.spv"), destroy_shader);
    auto compute = vkInfo<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    compute.stage = vkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute.stage.module = cluster.get();
    compute.stage.pName = "main";
    compute.layout = m_layout;
    check(vk.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &m_cluster_pipeline),
          "Create cluster compute pipeline");

    check(vk.vkEndCommandBuffer(m_upload_commands), "End static resource upload");
    auto submit = vkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_upload_commands;
    // The first frame is submitted to the same queue. Its fence also retires the staging buffers.
    check(vk.vkQueueSubmit(m_gpu.queue, 1, &submit, VK_NULL_HANDLE), "Submit static resource upload");
}

void VulkanForwardPasses::createLightingDescriptors() {
    const auto &vk = m_gpu.vk;
    const auto device = m_gpu.device;
    const float radius = m_camera.cluster_params[0] / 0.05F;
    const auto &eye = m_camera.eye;
    const auto &forward = m_camera.clip_w;
    const auto light = [&](Vector3 offset, float range, std::array<float, 3> color, float intensity) {
        const auto position = Vector3{eye[0], eye[1], eye[2]} +
                              Vector3{forward[0], forward[1], forward[2]} * (radius * 2.6F) + offset * radius;
        return PointLight{{position.x, position.y, position.z, radius * range},
                          {color[0], color[1], color[2], intensity * radius * radius}};
    };
    const std::array lights{
        light({-0.42F, 0.48F, 0.20F}, 0.75F, {1.0F, 0.55F, 0.20F}, 1.8F),
        light({0.42F, 0.28F, 0.38F}, 0.72F, {0.20F, 0.45F, 1.0F}, 1.6F),
        light({0.03F, 0.41F, -0.42F}, 0.62F, {0.20F, 1.0F, 0.48F}, 1.0F),
    };
    static_assert(lights.size() <= STAR_CLUSTER_MAX_LIGHTS);
    uploadBuffer(m_lights, std::as_bytes(std::span(lights)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    constexpr auto cluster_bytes = STAR_CLUSTER_COLUMNS * STAR_CLUSTER_ROWS * STAR_CLUSTER_SLICES *
                                   (1 + STAR_CLUSTER_MAX_LIGHTS) * sizeof(std::uint32_t);
    m_cluster_lists.create(cluster_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::GpuOnly);

    constexpr auto lighting_stages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    constexpr std::array bindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, lighting_stages, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    auto layout = vkInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    check(vk.vkCreateDescriptorSetLayout(device, &layout, nullptr, &m_descriptor_layout), "Create descriptor layout");
    constexpr std::array pool_sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1},
    };
    auto pool = vkInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = 1;
    pool.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    pool.pPoolSizes = pool_sizes.data();
    check(vk.vkCreateDescriptorPool(device, &pool, nullptr, &m_descriptor_pool), "Create descriptor pool");
    auto sets = vkInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    sets.descriptorPool = m_descriptor_pool;
    sets.descriptorSetCount = 1;
    sets.pSetLayouts = &m_descriptor_layout;
    check(vk.vkAllocateDescriptorSets(device, &sets, &m_descriptors), "Allocate lighting descriptor set");
    const std::array buffers{
        VkDescriptorBufferInfo{m_lights.handle, 0, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{m_cluster_lists.handle, 0, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{m_cluster_lists.handle, 0, VK_WHOLE_SIZE},
        VkDescriptorBufferInfo{m_shadow_buffer.handle, 0, VK_WHOLE_SIZE},
    };
    std::array<VkWriteDescriptorSet, bindings.size()> writes{};
    for (std::uint32_t binding = 0; binding < writes.size(); ++binding) {
        auto &write = writes[binding];
        write = vkInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = m_descriptors;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = bindings[binding].descriptorType;
    }
    for (std::size_t index = 0; index < buffers.size(); ++index) {
        writes[index].pBufferInfo = &buffers[index];
    }
    const VkDescriptorImageInfo shadow_image{VK_NULL_HANDLE, m_shadows.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &shadow_image;
    const VkDescriptorBufferInfo environment_buffer{m_environment_buffer.handle, 0, VK_WHOLE_SIZE};
    writes[5].pBufferInfo = &environment_buffer;
    const VkDescriptorImageInfo environment_image{VK_NULL_HANDLE, m_environment.view,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo environment_sampler{m_environment_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    writes[6].pImageInfo = &environment_image;
    writes[7].pImageInfo = &environment_sampler;
    vk.vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanForwardPasses::createShadows() {
    VkFormatProperties format{};
    m_gpu.inst.vkGetPhysicalDeviceFormatProperties(m_gpu.physicalDevice, depth_format, &format);
    constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((format.optimalTilingFeatures & required) != required) {
        throw std::runtime_error("The device does not support sampled D32 depth images");
    }
    m_shadow_frame = makeShadowFrame(m_camera);
    uploadBuffer(m_shadow_buffer, std::as_bytes(std::span(&m_shadow_frame, 1)), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    m_shadows.create(depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT, shadow_resolution, STAR_SHADOW_CASCADES);
    for (std::uint32_t layer = 0; layer < m_shadow_layers.size(); ++layer) {
        auto view = vkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = m_shadows.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = depth_format;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
        check(m_gpu.vk.vkCreateImageView(m_gpu.device, &view, nullptr, &m_shadow_layers[layer]),
              "Create shadow layer view");
    }
}

void VulkanForwardPasses::createEnvironment() {
    VkFormatProperties format{};
    m_gpu.inst.vkGetPhysicalDeviceFormatProperties(m_gpu.physicalDevice, environment_format, &format);
    constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((format.optimalTilingFeatures & required) != required) {
        throw std::runtime_error("The device does not support filtered RGBA32F environment images");
    }

    const auto lighting = buildBuiltinEnvironment();
    const auto &base = lighting.mip_levels.front();
    const auto mip_count = static_cast<std::uint32_t>(lighting.mip_levels.size());
    m_environment.create(environment_format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, {base.width, base.height}, 1, mip_count);
    uploadBuffer(m_environment_buffer, std::as_bytes(std::span(lighting.diffuse)), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    std::vector<VkBufferImageCopy> regions;
    for (std::uint32_t level = 0; level < mip_count; ++level) {
        const auto &mip = lighting.mip_levels[level];
        VkBufferImageCopy region{};
        region.bufferOffset = mip.first_pixel * sizeof(EnvironmentPixel);
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        region.imageExtent = {mip.width, mip.height, 1};
        regions.push_back(region);
    }

    uploadImage(m_environment, std::as_bytes(std::span(lighting.specular)), regions);

    const auto &vk = m_gpu.vk;
    auto sampler = vkInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = static_cast<float>(mip_count - 1);
    check(vk.vkCreateSampler(m_gpu.device, &sampler, nullptr, &m_environment_sampler), "Create environment sampler");
}

VkBuffer VulkanForwardPasses::stageUpload(std::span<const std::byte> bytes) {
    auto staging = std::make_unique<Buffer>(m_gpu);
    staging->upload(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    m_staging.push_back(std::move(staging));
    return m_staging.back()->handle;
}

void VulkanForwardPasses::uploadBuffer(Buffer &buffer, std::span<const std::byte> bytes, VkBufferUsageFlags usage,
                                       VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
    buffer.create(bytes.size(), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::GpuOnly);
    const auto staging = stageUpload(bytes);
    const auto &vk = m_gpu.vk;
    const VkBufferCopy copy{0, 0, bytes.size()};
    vk.vkCmdCopyBuffer(m_upload_commands, staging, buffer.handle, 1, &copy);

    auto barrier = vkInfo<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = stages;
    barrier.dstAccessMask = access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.handle;
    barrier.size = VK_WHOLE_SIZE;
    auto dependency = vkInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vk.vkCmdPipelineBarrier2(m_upload_commands, &dependency);
}

void VulkanForwardPasses::uploadImage(const Image &image, std::span<const std::byte> bytes,
                                      std::span<const VkBufferImageCopy> regions) {
    const auto staging = stageUpload(bytes);
    transitionVulkanImage(m_gpu, m_upload_commands, image.handle, image.range,
                          vulkanTextureSync(TextureState::Undefined), vulkanTextureSync(TextureState::CopyDestination));
    m_gpu.vk.vkCmdCopyBufferToImage(m_upload_commands, staging, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    static_cast<std::uint32_t>(regions.size()), regions.data());
    transitionVulkanImage(m_gpu, m_upload_commands, image.handle, image.range,
                          vulkanTextureSync(TextureState::CopyDestination), vulkanTextureSync(TextureState::Sampled));
}

void VulkanForwardPasses::createMaterialDescriptors(const StaticModelAsset &model) {
    for (const auto &texture : model.textures) {
        auto image = std::make_unique<Image>(m_gpu);
        const auto &base = texture.mips.front();
        const auto format =
            texture.usage == TextureColorSpace::Color ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        image->create(format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                      {base.width, base.height}, 1, static_cast<std::uint32_t>(texture.mips.size()));
        std::vector<std::uint8_t> bytes;
        std::vector<VkBufferImageCopy> regions;
        for (std::uint32_t level = 0; level < texture.mips.size(); ++level) {
            const auto &mip = texture.mips[level];
            VkBufferImageCopy region{};
            region.bufferOffset = bytes.size();
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            region.imageExtent = {mip.width, mip.height, 1};
            regions.push_back(region);
            bytes.insert(bytes.end(), mip.rgba.begin(), mip.rgba.end());
        }
        uploadImage(*image, std::as_bytes(std::span(bytes)), regions);
        m_material_images.push_back(std::move(image));
    }
    const auto fallback_index = m_material_images.size();
    auto white_image = std::make_unique<Image>(m_gpu);
    white_image->create(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, {1, 1});
    constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    uploadImage(*white_image, std::as_bytes(std::span(white)), std::span(&region, 1));
    m_material_images.push_back(std::move(white_image));

    const auto &vk = m_gpu.vk;
    const auto device = m_gpu.device;
    auto sampler = vkInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    check(vk.vkCreateSampler(device, &sampler, nullptr, &m_material_sampler), "Create material sampler");

    constexpr std::array bindings{
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    auto layout = vkInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    check(vk.vkCreateDescriptorSetLayout(device, &layout, nullptr, &m_material_layout),
          "Create material descriptor layout");
    if (model.parts.size() > std::numeric_limits<std::uint32_t>::max() / 2) {
        throw std::runtime_error("Model exceeds descriptor limits");
    }
    const auto count = static_cast<std::uint32_t>(model.parts.size());
    const std::array sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, count * 2},
                           VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, count}};
    auto pool = vkInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = count;
    pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    check(vk.vkCreateDescriptorPool(device, &pool, nullptr, &m_material_pool), "Create material descriptor pool");

    std::map<std::pair<std::size_t, std::size_t>, VkDescriptorSet> allocated;
    for (std::size_t index = 0; index < model.parts.size(); ++index) {
        const auto &part = model.parts[index];
        const auto material = part.material.value_or(StaticPbrMaterial{});
        const auto key = std::pair{material.base_color_texture.value_or(fallback_index),
                                   material.metallic_roughness_texture.value_or(fallback_index)};
        auto [it, inserted] = allocated.try_emplace(key, VK_NULL_HANDLE);
        if (inserted) {
            auto sets = vkInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            sets.descriptorPool = m_material_pool;
            sets.descriptorSetCount = 1;
            sets.pSetLayouts = &m_material_layout;
            check(vk.vkAllocateDescriptorSets(device, &sets, &it->second), "Allocate material descriptor set");
            const std::array images{
                VkDescriptorImageInfo{VK_NULL_HANDLE, m_material_images.at(key.first)->view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                VkDescriptorImageInfo{VK_NULL_HANDLE, m_material_images.at(key.second)->view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                VkDescriptorImageInfo{m_material_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
            };
            std::array<VkWriteDescriptorSet, bindings.size()> writes{};
            for (std::uint32_t binding = 0; binding < writes.size(); ++binding) {
                auto &write = writes[binding];
                write = vkInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
                write.dstSet = it->second;
                write.dstBinding = binding;
                write.descriptorCount = 1;
                write.descriptorType = bindings[binding].descriptorType;
                write.pImageInfo = &images[binding];
            }
            vk.vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
        m_parts[index].textures = it->second;
    }
}

void VulkanForwardPasses::dispatchClusters() const {
    static_assert(STAR_CLUSTER_COLUMNS % STAR_CLUSTER_WORKGROUP_X == 0);
    static_assert(STAR_CLUSTER_ROWS % STAR_CLUSTER_WORKGROUP_Y == 0);
    static_assert(STAR_CLUSTER_SLICES % STAR_CLUSTER_WORKGROUP_Z == 0);
    const auto &vk = m_gpu.vk;
    vk.vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_cluster_pipeline);
    vk.vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_descriptors, 0, nullptr);
    vk.vkCmdPushConstants(m_commands, m_layout, all_shader_stages, 0, sizeof(m_camera), &m_camera);
    vk.vkCmdDispatch(m_commands, STAR_CLUSTER_COLUMNS / STAR_CLUSTER_WORKGROUP_X,
                     STAR_CLUSTER_ROWS / STAR_CLUSTER_WORKGROUP_Y, STAR_CLUSTER_SLICES / STAR_CLUSTER_WORKGROUP_Z);
    auto barrier = vkInfo<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_cluster_lists.handle;
    barrier.size = VK_WHOLE_SIZE;
    auto dependency = vkInfo<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vk.vkCmdPipelineBarrier2(m_commands, &dependency);
}

VkShaderModule VulkanForwardPasses::loadShader(const std::filesystem::path &path) const {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = static_cast<std::streamoff>(input.tellg());
    if (!input || size <= 0 || size % 4 != 0 ||
        static_cast<std::uintmax_t>(size / 4) > std::vector<std::uint32_t>().max_size() ||
        size > std::numeric_limits<std::streamsize>::max() || !input.seekg(0)) {
        throw std::runtime_error("Invalid SPIR-V file " + path.string());
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size / 4));
    if (!input.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("Cannot read SPIR-V file " + path.string());
    }
    auto info = vkInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = static_cast<std::size_t>(size);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(m_gpu.vk.vkCreateShaderModule(m_gpu.device, &info, nullptr, &module), "Create shader module");
    return module;
}

VkPipeline VulkanForwardPasses::createPipeline(VkShaderModule vertex, VkShaderModule fragment, VkExtent2D extent,
                                               bool double_sided) const {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        vkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO),
        vkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO)};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";
    constexpr VkVertexInputBindingDescription binding{0, sizeof(StaticMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    constexpr std::array attributes{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticMeshVertex, uv)}};
    auto input =
        vkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    auto assembly =
        vkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    auto viewport_state =
        vkInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    auto raster =
        vkInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.cullMode = double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    raster.lineWidth = 1.0F;
    auto sampling =
        vkInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    sampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto depth =
        vkInfo<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = fragment ? VK_FALSE : VK_TRUE;
    depth.depthCompareOp = fragment ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto blend = vkInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = fragment ? 1 : 0;
    blend.pAttachments = &attachment;
    auto attachments = vkInfo<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    attachments.colorAttachmentCount = fragment ? 1 : 0;
    attachments.pColorAttachmentFormats = &color_format;
    attachments.depthAttachmentFormat = depth_format;
    auto info = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    info.pNext = &attachments;
    info.stageCount = fragment ? 2 : 1;
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &sampling;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.layout = m_layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(m_gpu.vk.vkCreateGraphicsPipelines(m_gpu.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "Create graphics pipeline");
    return pipeline;
}

void VulkanForwardPasses::renderPass(bool depth_only) const {
    const auto &vk = m_gpu.vk;
    auto color = vkInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = m_color.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.025F, 0.031F, 0.046F, 1.0F}};
    auto depth = vkInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    depth.imageView = m_depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = depth_only ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = depth_only ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0F, 0};
    auto rendering = vkInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea = {{0, 0}, resolution};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = depth_only ? 0 : 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vk.vkCmdBeginRendering(m_commands, &rendering);
    vk.vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_only ? m_prepass : m_forward);
    if (!depth_only) {
        vk.vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_descriptors, 0,
                                   nullptr);
    }
    drawParts(m_camera, depth_only ? DrawPass::Depth : DrawPass::Color);
    vk.vkCmdEndRendering(m_commands);
}

void VulkanForwardPasses::renderShadows() const {
    const auto &vk = m_gpu.vk;
    auto depth = vkInfo<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0F, 0};
    auto rendering = vkInfo<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    rendering.renderArea = {{0, 0}, shadow_resolution};
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depth;
    for (std::size_t layer = 0; layer < m_shadow_layers.size(); ++layer) {
        depth.imageView = m_shadow_layers[layer];
        vk.vkCmdBeginRendering(m_commands, &rendering);
        vk.vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadow_pipeline);
        DrawConstants view{};
        view.clip_x = m_shadow_frame.views[layer].clip_x;
        view.clip_y = m_shadow_frame.views[layer].clip_y;
        view.clip_z = m_shadow_frame.views[layer].clip_z;
        view.clip_w = {0, 0, 0, 1};
        drawParts(view, DrawPass::Shadow);
        vk.vkCmdEndRendering(m_commands);
    }
}

void VulkanForwardPasses::drawParts(const DrawConstants &frame, DrawPass pass) const {
    const auto &vk = m_gpu.vk;
    constexpr VkDeviceSize offset = 0;
    vk.vkCmdBindVertexBuffers(m_commands, 0, 1, &m_vertices.handle, &offset);
    vk.vkCmdBindIndexBuffer(m_commands, m_indices.handle, 0, VK_INDEX_TYPE_UINT32);
    for (const auto &part : m_parts) {
        if (pass == DrawPass::Shadow && !part.cast_shadows) {
            continue;
        }
        if (pass == DrawPass::Color) {
            vk.vkCmdBindDescriptorSets(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 1, 1, &part.textures, 0,
                                       nullptr);
        }
        auto push = frame;
        push.albedo = part.albedo;
        push.factors = part.factors;
        vk.vkCmdPushConstants(m_commands, m_layout, all_shader_stages, 0, sizeof(push), &push);
        vk.vkCmdDrawIndexed(m_commands, part.index_count, 1, part.index_offset, part.vertex_offset, 0);
    }
}

double VulkanForwardPasses::draw() {
    const auto &vk = m_gpu.vk;
    check(vk.vkResetCommandBuffer(m_commands, 0), "Reset command buffer");
    const auto begin = vkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    check(vk.vkBeginCommandBuffer(m_commands, &begin), "Begin command buffer");
    if (m_queries) {
        vk.vkCmdResetQueryPool(m_commands, m_queries, 0, 2);
        vk.vkCmdWriteTimestamp2(m_commands, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_queries, 0);
    }
    const auto transition = [&](const Image &image, TextureState before, TextureState after) {
        transitionVulkanImage(m_gpu, m_commands, image.handle, image.range, vulkanTextureSync(before),
                              vulkanTextureSync(after));
    };
    transition(m_shadows, TextureState::Undefined, TextureState::DepthAttachment);
    renderShadows();
    transition(m_shadows, TextureState::DepthAttachment, TextureState::Sampled);
    transition(m_depth, TextureState::Undefined, TextureState::DepthAttachment);
    renderPass(true);
    dispatchClusters();
    transition(m_depth, TextureState::DepthAttachment, TextureState::DepthAttachment);
    transition(m_color, TextureState::Undefined, TextureState::ColorAttachment);
    renderPass(false);
    transition(m_color, TextureState::ColorAttachment, TextureState::CopySource);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {resolution.width, resolution.height, 1};
    vk.vkCmdCopyImageToBuffer(m_commands, m_color.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_capture.handle, 1,
                              &copy);
    if (m_queries) {
        vk.vkCmdWriteTimestamp2(m_commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queries, 1);
    }
    submitCommands();
    m_staging.clear();
    if (m_gpu.validationErrors.load() != 0) {
        throw std::runtime_error("Vulkan validation reported errors");
    }
    if (!m_queries) {
        return 0.0;
    }
    std::array<std::uint64_t, 2> ticks{};
    check(vk.vkGetQueryPoolResults(m_gpu.device, m_queries, 0, 2, sizeof(ticks), ticks.data(), sizeof(ticks[0]),
                                   VK_QUERY_RESULT_64_BIT),
          "Read timestamps");
    const auto bits = m_gpu.queueProperties.timestampValidBits;
    const auto mask = bits == 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
    return static_cast<double>((ticks[1] - ticks[0]) & mask) * m_gpu.properties.limits.timestampPeriod / 1e6;
}

void VulkanForwardPasses::submitCommands() const {
    const auto &vk = m_gpu.vk;
    check(vk.vkEndCommandBuffer(m_commands), "End command buffer");
    auto submit = vkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commands;
    check(vk.vkResetFences(m_gpu.device, 1, &m_fence), "Reset fence");
    check(vk.vkQueueSubmit(m_gpu.queue, 1, &submit, m_fence), "Submit draw");
    check(vk.vkWaitForFences(m_gpu.device, 1, &m_fence, VK_TRUE, UINT64_MAX), "Wait for draw");
}

ForwardImage VulkanForwardPasses::capture() const {
    ForwardImage image{resolution.width, resolution.height, std::vector<std::uint8_t>(image_bytes)};
    check(vmaCopyAllocationToMemory(m_gpu.allocator, m_capture.allocation, 0, image.rgba.data(), image.rgba.size()),
          "Read color capture");
    return image;
}

} // namespace

struct ForwardRenderer::Impl {
    VulkanRuntime gpu;
    VulkanForwardPasses passes{gpu};
    bool rendered = false;
};

#else
struct ForwardRenderer::Impl {};
#endif

namespace {

bool validGeometry(const StaticModelAsset &model) {
    if (model.parts.empty()) {
        return false;
    }
    for (const auto &part : model.parts) {
        if (part.vertices.empty() || part.indices.empty() || part.indices.size() % 3 != 0 ||
            std::ranges::any_of(part.indices, [&](auto index) { return index >= part.vertices.size(); })) {
            return false;
        }
        if (part.material) {
            for (const auto index : {part.material->base_color_texture, part.material->metallic_roughness_texture}) {
                if (index && *index >= model.textures.size()) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool validTexture(const TextureAsset &texture) {
    if (texture.mips.empty() || texture.mips.size() > 32) {
        return false;
    }
    const TextureMip *previous = nullptr;
    for (const auto &mip : texture.mips) {
        if (mip.width == 0 || mip.height == 0 || std::uint64_t(mip.width) * mip.height != mip.rgba.size() / 4 ||
            mip.rgba.size() % 4 != 0) {
            return false;
        }
        if (previous &&
            ((previous->width == 1 && previous->height == 1) || mip.width != std::max(1U, previous->width / 2) ||
             mip.height != std::max(1U, previous->height / 2))) {
            return false;
        }
        previous = &mip;
    }
    return true;
}

} // namespace

ForwardMaterial defaultForwardMaterial() {
    ForwardMaterial material{};
    material.variant.shader = Guid::parse("6cab949113d24a3db5bb89eadb57769b").value();
    material.variant.state.double_sided = true;
    return material;
}

ForwardRenderer::ForwardRenderer(std::unique_ptr<Impl> implementation) noexcept : m_impl(std::move(implementation)) {}

ForwardRenderer::~ForwardRenderer() = default;

std::expected<std::unique_ptr<ForwardRenderer>, ForwardRenderError>
ForwardRenderer::create(const StaticModelAsset &model, const ForwardRenderOptions &options,
                        const ForwardMaterial &material) {
    if (!validGeometry(model) || !std::ranges::all_of(model.textures, validTexture) ||
        (!options.shadow_casters.empty() && options.shadow_casters.size() != model.parts.size())) {
        return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::InvalidInput, "Invalid static model"});
    }
    if (material.variant.shader != defaultForwardMaterial().variant.shader ||
        material.variant.state.blend != MaterialBlendMode::Opaque || !material.variant.state.depth_test ||
        material.variant.state.shading > MaterialShadingModel::Unlit) {
        return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::Unsupported, "Unsupported forward material"});
    }
#ifdef STAR_FORWARD_VULKAN
    try {
        auto implementation = std::make_unique<Impl>();
        if (const auto initialized = implementation->gpu.initialize(options.enable_validation);
            !initialized || (options.enable_validation && !implementation->gpu.validationEnabled)) {
            return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::BackendFailure,
                                                      "Cannot initialize the requested Vulkan device"});
        }
        implementation->passes.initialize(model, options, material);
        return std::unique_ptr<ForwardRenderer>(new ForwardRenderer(std::move(implementation)));
    } catch (const std::exception &error) {
        return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::BackendFailure, error.what()});
    }
#else
    (void)options;
    return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::Unsupported, "Vulkan renderer unavailable"});
#endif
}

std::expected<double, ForwardRenderError> ForwardRenderer::render() {
#ifdef STAR_FORWARD_VULKAN
    try {
        const auto duration = m_impl->passes.draw();
        m_impl->rendered = true;
        return duration;
    } catch (const std::exception &error) {
        return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::BackendFailure, error.what()});
    }
#else
    return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::Unsupported, "Vulkan renderer unavailable"});
#endif
}

std::expected<ForwardImage, ForwardRenderError> ForwardRenderer::capture() const {
#ifdef STAR_FORWARD_VULKAN
    if (!m_impl->rendered) {
        return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::InvalidInput, "No frame has been rendered"});
    }
    try {
        return m_impl->passes.capture();
    } catch (const std::exception &error) {
        return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::BackendFailure, error.what()});
    }
#else
    return std::unexpected(ForwardRenderError{ForwardRenderErrorCode::Unsupported, "Vulkan renderer unavailable"});
#endif
}

std::string_view ForwardRenderer::getDeviceName() const noexcept {
#ifdef STAR_FORWARD_VULKAN
    return m_impl->gpu.properties.deviceName;
#else
    return {};
#endif
}

} // namespace star::renderer
