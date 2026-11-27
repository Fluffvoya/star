#include "asset-pipeline/model_importer.hpp"
#include "cluster_sizes.h"
#include "environment.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace star::asset_pipeline;
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

std::vector<std::byte> readFile(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Cannot open " + path.string());
    }
    const auto size = static_cast<std::streamoff>(input.tellg());
    if (size <= 0 || static_cast<std::uintmax_t>(size) > std::vector<std::byte>().max_size() ||
        size > std::numeric_limits<std::streamsize>::max() || !input.seekg(0)) {
        throw std::runtime_error("Invalid file size for " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return bytes;
}

ImportedStaticModel importModel(const std::filesystem::path &path) {
    const auto bytes = readFile(path);
    const auto extension = path.extension();
    if (extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".fbx") {
        throw std::runtime_error("Expected a .gltf, .glb, .obj or .fbx model");
    }
    auto model =
        extension == ".gltf" || extension == ".glb"
            ? importGltfStaticModel(bytes, [&](std::string_view name) { return readFile(path.parent_path() / name); })
            : importStaticModel(bytes, extension == ".fbx" ? StaticModelFormat::Fbx : StaticModelFormat::Obj);
    if (!model) {
        throw std::runtime_error(model.error().message);
    }
    return std::move(*model);
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
        check(vmaCopyMemoryToAllocation(gpu.allocator, bytes.data(), allocation, 0, bytes.size()), "Upload mesh data");
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

struct DrawPart {
    std::uint32_t index_count;
    std::uint32_t index_offset;
    std::int32_t vertex_offset;
    std::array<float, 4> albedo;
    std::array<float, 4> factors;
};

DrawConstants makeCamera(std::span<const StaticMeshVertex> vertices) {
    using namespace star::math;
    Vector3 low = vertices.front().position;
    Vector3 high = low;
    for (const auto &vertex : vertices) {
        const auto &p = vertex.position;
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const auto center = (low + high) * 0.5F;
    const float radius = std::max(length(high - low) * 0.5F, 0.05F);
    const auto eye = center + normalize(Vector3{0.9F, 0.62F, 1.0F}) * (radius * 2.6F);
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

class ForwardSample {
  public:
    explicit ForwardSample(const VulkanRuntime &gpu)
        : m_gpu(gpu), m_vertices{gpu}, m_indices{gpu}, m_capture{gpu}, m_lights{gpu}, m_cluster_lists{gpu},
          m_shadow_buffer{gpu}, m_environment_buffer{gpu}, m_color{gpu}, m_depth{gpu}, m_shadows{gpu},
          m_environment{gpu} {}

    ~ForwardSample() {
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
        vk.vkDestroyDescriptorSetLayout(device, m_descriptor_layout, nullptr);
        vk.vkDestroySampler(device, m_environment_sampler, nullptr);
        vk.vkDestroyQueryPool(device, m_queries, nullptr);
        vk.vkDestroyFence(device, m_fence, nullptr);
        vk.vkDestroyCommandPool(device, m_pool, nullptr);
    }

    void initialize(const ImportedStaticModel &model, const std::filesystem::path &assets);
    double draw();
    void saveCapture(const std::filesystem::path &path) const;

  private:
    VkPipeline createPipeline(VkShaderModule vertex, VkShaderModule fragment, VkExtent2D extent) const;
    VkShaderModule loadShader(const std::filesystem::path &path) const;
    void renderPass(bool depth_only) const;
    void createLightingDescriptors();
    void createShadows();
    void createEnvironment();
    void submitCommands() const;
    void renderShadows() const;
    void drawParts(const DrawConstants &frame) const;
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
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
};

void ForwardSample::initialize(const ImportedStaticModel &model, const std::filesystem::path &assets) {
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
        if (part.name == "Copper") {
            draw.albedo = {0.95F, 0.56F, 0.37F, 1};
            draw.factors = {0.26F, 0.85F, 0, 0};
        } else if (part.name == "Ceramic") {
            draw.albedo = {0.10F, 0.58F, 0.55F, 1};
            draw.factors[0] = 0.34F;
        } else if (part.name == "Paint") {
            draw.albedo = {0.60F, 0.20F, 0.09F, 1};
        }
        m_parts.push_back(draw);
        vertices.insert(vertices.end(), part.vertices.begin(), part.vertices.end());
        indices.insert(indices.end(), part.indices.begin(), part.indices.end());
    }
    m_camera = makeCamera(vertices);
    m_vertices.upload(std::as_bytes(std::span(vertices)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_indices.upload(std::as_bytes(std::span(indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
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
    commands.commandBufferCount = 1;
    check(vk.vkAllocateCommandBuffers(device, &commands, &m_commands), "Allocate command buffer");
    const auto fence = vkInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    check(vk.vkCreateFence(device, &fence, nullptr, &m_fence), "Create submission fence");
    if (m_gpu.queueProperties.timestampValidBits != 0) {
        auto query = vkInfo<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
        query.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query.queryCount = 2;
        check(vk.vkCreateQueryPool(device, &query, nullptr, &m_queries), "Create timestamp queries");
    }
    createShadows();
    createEnvironment();
    createLightingDescriptors();
    const VkPushConstantRange constants{all_shader_stages, 0, sizeof(DrawConstants)};
    auto layout = vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &m_descriptor_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    check(vk.vkCreatePipelineLayout(device, &layout, nullptr, &m_layout), "Create pipeline layout");

    const auto destroy_shader = [&](VkShaderModule shader) { vk.vkDestroyShaderModule(device, shader, nullptr); };
    using Shader = std::unique_ptr<std::remove_pointer_t<VkShaderModule>, decltype(destroy_shader)>;
    const Shader vertex(loadShader(assets / "forward-vertex.spv"), destroy_shader);
    const Shader fragment(loadShader(assets / "forward-fragment.spv"), destroy_shader);
    m_prepass = createPipeline(vertex.get(), VK_NULL_HANDLE, resolution);
    m_forward = createPipeline(vertex.get(), fragment.get(), resolution);
    m_shadow_pipeline = createPipeline(vertex.get(), VK_NULL_HANDLE, shadow_resolution);
    const Shader cluster(loadShader(assets / "forward-cluster.spv"), destroy_shader);
    auto compute = vkInfo<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    compute.stage = vkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute.stage.module = cluster.get();
    compute.stage.pName = "main";
    compute.layout = m_layout;
    check(vk.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &m_cluster_pipeline),
          "Create cluster compute pipeline");
}

void ForwardSample::createLightingDescriptors() {
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
    m_lights.upload(std::as_bytes(std::span(lights)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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

void ForwardSample::createShadows() {
    VkFormatProperties format{};
    m_gpu.inst.vkGetPhysicalDeviceFormatProperties(m_gpu.physicalDevice, depth_format, &format);
    constexpr auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((format.optimalTilingFeatures & required) != required) {
        throw std::runtime_error("The device does not support sampled D32 depth images");
    }
    m_shadow_frame = makeShadowFrame(m_camera);
    m_shadow_buffer.upload(std::as_bytes(std::span(&m_shadow_frame, 1)), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
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

void ForwardSample::createEnvironment() {
    VkFormatProperties format{};
    m_gpu.inst.vkGetPhysicalDeviceFormatProperties(m_gpu.physicalDevice, environment_format, &format);
    constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((format.optimalTilingFeatures & required) != required) {
        throw std::runtime_error("The device does not support filtered RGBA32F environment images");
    }

    const auto lighting = star::sample::buildSampleEnvironment();
    const auto &base = lighting.mip_levels.front();
    const auto mip_count = static_cast<std::uint32_t>(lighting.mip_levels.size());
    m_environment.create(environment_format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, {base.width, base.height}, 1, mip_count);
    m_environment_buffer.upload(std::as_bytes(std::span(lighting.diffuse)), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Buffer staging(m_gpu);
    staging.upload(std::as_bytes(std::span(lighting.specular)), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::vector<VkBufferImageCopy> regions;
    for (std::uint32_t level = 0; level < mip_count; ++level) {
        const auto &mip = lighting.mip_levels[level];
        VkBufferImageCopy region{};
        region.bufferOffset = mip.first_pixel * sizeof(star::sample::EnvironmentPixel);
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        region.imageExtent = {mip.width, mip.height, 1};
        regions.push_back(region);
    }

    const auto &vk = m_gpu.vk;
    const auto begin = vkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    check(vk.vkBeginCommandBuffer(m_commands, &begin), "Begin environment upload");
    transitionVulkanImage(m_gpu, m_commands, m_environment.handle, m_environment.range,
                          vulkanTextureSync(TextureState::Undefined), vulkanTextureSync(TextureState::CopyDestination));
    vk.vkCmdCopyBufferToImage(m_commands, staging.handle, m_environment.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              static_cast<std::uint32_t>(regions.size()), regions.data());
    transitionVulkanImage(m_gpu, m_commands, m_environment.handle, m_environment.range,
                          vulkanTextureSync(TextureState::CopyDestination), vulkanTextureSync(TextureState::Sampled));
    submitCommands();

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

void ForwardSample::dispatchClusters() const {
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

VkShaderModule ForwardSample::loadShader(const std::filesystem::path &path) const {
    const auto bytes = readFile(path);
    if (bytes.size() % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V bytecode size");
    }
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    auto info = vkInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = bytes.size();
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(m_gpu.vk.vkCreateShaderModule(m_gpu.device, &info, nullptr, &module), "Create shader module");
    return module;
}

VkPipeline ForwardSample::createPipeline(VkShaderModule vertex, VkShaderModule fragment, VkExtent2D extent) const {
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
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, normal)}};
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

void ForwardSample::renderPass(bool depth_only) const {
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
    drawParts(m_camera);
    vk.vkCmdEndRendering(m_commands);
}

void ForwardSample::renderShadows() const {
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
        drawParts(view);
        vk.vkCmdEndRendering(m_commands);
    }
}

void ForwardSample::drawParts(const DrawConstants &frame) const {
    const auto &vk = m_gpu.vk;
    constexpr VkDeviceSize offset = 0;
    vk.vkCmdBindVertexBuffers(m_commands, 0, 1, &m_vertices.handle, &offset);
    vk.vkCmdBindIndexBuffer(m_commands, m_indices.handle, 0, VK_INDEX_TYPE_UINT32);
    for (const auto &part : m_parts) {
        auto push = frame;
        push.albedo = part.albedo;
        push.factors = part.factors;
        vk.vkCmdPushConstants(m_commands, m_layout, all_shader_stages, 0, sizeof(push), &push);
        vk.vkCmdDrawIndexed(m_commands, part.index_count, 1, part.index_offset, part.vertex_offset, 0);
    }
}

double ForwardSample::draw() {
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

void ForwardSample::submitCommands() const {
    const auto &vk = m_gpu.vk;
    check(vk.vkEndCommandBuffer(m_commands), "End command buffer");
    auto submit = vkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commands;
    check(vk.vkResetFences(m_gpu.device, 1, &m_fence), "Reset fence");
    check(vk.vkQueueSubmit(m_gpu.queue, 1, &submit, m_fence), "Submit draw");
    check(vk.vkWaitForFences(m_gpu.device, 1, &m_fence, VK_TRUE, UINT64_MAX), "Wait for draw");
}

void ForwardSample::saveCapture(const std::filesystem::path &path) const {
    std::vector<char> rgba(image_bytes);
    check(vmaCopyAllocationToMemory(m_gpu.allocator, m_capture.allocation, 0, rgba.data(), rgba.size()),
          "Read color capture");
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << resolution.width << ' ' << resolution.height << "\n255\n";
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        output.write(rgba.data() + i, 3);
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc > 4) {
            throw std::runtime_error("Usage: star-vulkan-forward [model] [capture.ppm] [frames]");
        }
        const auto assets = std::filesystem::canonical(argv[0]).parent_path();
        const auto source = argc > 1 ? std::filesystem::path(argv[1]) : assets / "forward-scene.obj";
        const auto capture = argc > 2 ? std::filesystem::path(argv[2]) : "forward-capture.ppm";
        unsigned frames = 10;
        if (argc > 3) {
            const std::string_view text(argv[3]);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), frames);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || frames == 0) {
                throw std::runtime_error("Frames must be a positive integer");
            }
        }
        const auto model = importModel(source);
        VulkanRuntime gpu;
        if (const auto initialized = gpu.initialize(true); !initialized || !gpu.validationEnabled) {
            throw std::runtime_error("A Vulkan device with validation is required");
        }
        ForwardSample sample(gpu);
        sample.initialize(model, assets);
        const auto warmup_ms = sample.draw();
        double total_ms = 0.0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            total_ms += sample.draw();
        }
        sample.saveCapture(capture);
        std::printf("Vulkan %s, validation enabled, %ux%u, %zu mesh parts\n", gpu.properties.deviceName,
                    resolution.width, resolution.height, model.parts.size());
        if (total_ms > 0.0) {
            std::printf("%u frames, %.3f ms average GPU time, %.3f ms warmup\n", frames, total_ms / frames, warmup_ms);
        }
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
