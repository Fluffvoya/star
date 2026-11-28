#include "scene_models.hpp"

#include "core/log.hpp"
#include "platform/mapped_file.hpp"

#if defined(STAR_EDITOR_MODEL_IMPORT)
#include "asset-pipeline/cached_model_import.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

namespace star::editor {
#if defined(STAR_EDITOR_MODEL_IMPORT)
namespace {

const LogCategory k_log_category("SceneModels", LogLevel::Info);
#if defined(_WIN32)
constexpr std::string_view k_asset_platform = "windows";
#elif defined(__APPLE__)
constexpr std::string_view k_asset_platform = "macos";
#else
constexpr std::string_view k_asset_platform = "linux";
#endif

struct DrawConstants {
    math::Mat4 clip_from_model;
    std::array<float, 4> color;
};
static_assert(sizeof(DrawConstants) == 20 * sizeof(float));

[[nodiscard]] rhi::ShaderModuleHandle loadShader(rhi::Device &device, const std::filesystem::path &path,
                                                 rhi::ShaderStage stage) {
    const auto file = MappedFile::open(path);
    if (!file || file->getBytes().empty() || file->getBytes().size() % sizeof(std::uint32_t) != 0) {
        return {};
    }
    const auto bytes = file->getBytes();
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return device.createShaderModule({stage, words, "Scene Model Shader"}).value_or(rhi::ShaderModuleHandle{});
}

} // namespace
#endif

struct SceneModels::Impl {
    struct Part {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
        std::uint32_t count;
        std::array<float, 4> color;
    };

    explicit Impl(rhi::Device &owner) : device(owner) {}

    rhi::Device &device;
    rhi::ShaderModuleHandle vertex_shader;
    rhi::ShaderModuleHandle fragment_shader;
    rhi::PipelineLayoutHandle layout;
    rhi::GraphicsPipelineHandle pipeline;
    std::filesystem::path root;
    std::map<Guid, std::vector<Part>> models;

#if defined(STAR_EDITOR_MODEL_IMPORT)
    asset_pipeline::AssetDatabase assets;
    bool scanned = false;

    [[nodiscard]] std::expected<std::vector<std::byte>, asset_pipeline::ModelImportError>
    readGltfDependency(const std::filesystem::path &source, std::string_view uri) const {
        const std::filesystem::path relative(uri);
        std::error_code error;
        const auto content = std::filesystem::canonical(root / "Content", error);
        if (error || relative.has_root_path()) {
            return std::unexpected(asset_pipeline::ModelImportError{"Invalid glTF resource path"});
        }
        const auto resource = std::filesystem::canonical(source.parent_path() / relative, error);
        const auto within_content = resource.lexically_relative(content);
        if (error || within_content.empty() || *within_content.begin() == "..") {
            return std::unexpected(asset_pipeline::ModelImportError{"glTF resource is outside project Content"});
        }
        const auto data = MappedFile::open(resource);
        if (!data) {
            return std::unexpected(asset_pipeline::ModelImportError{"Cannot read glTF resource " + resource.string()});
        }
        const auto bytes = data->getBytes();
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::expected<asset_pipeline::ImportedStaticModel, asset_pipeline::ModelImportError>
    import(Guid guid) {
        if (!scanned) {
            scanned = true;
            const auto content = root / "Content";
            if (const auto scan = assets.scan(content); !scan) {
                return std::unexpected(asset_pipeline::ModelImportError{"Cannot index " + scan.error().path.string()});
            }
        }
        const auto *location = assets.findAsset(guid);
        if (!location) {
            return std::unexpected(asset_pipeline::ModelImportError{"No asset metadata matches the mesh GUID"});
        }
        const auto *meta = assets.findMeta(location->source);
        const auto source = root / "Content" / location->source;
        const auto bytes = MappedFile::open(source);
        if (!meta || !bytes) {
            return std::unexpected(asset_pipeline::ModelImportError{"Cannot read model " + source.string()});
        }
        using namespace asset_pipeline;
        auto imported =
            meta->importer == "star::GltfImporter" && source.extension() == ".gltf"
                ? importGltfTexturedModel(bytes->getBytes(),
                                          [&](std::string_view uri) { return readGltfDependency(source, uri); })
                : importCachedModel(bytes->getBytes(), *meta, k_asset_platform,
                                    DerivedDataCache(root / "Intermediate" / "DDC" / k_asset_platform))
                      .transform([](CachedModel cached) { return std::move(cached.model); });
        if (imported && location->sub_asset_index) {
            const auto &part_name = meta->sub_assets[*location->sub_asset_index].name;
            auto &parts = imported->parts;
            const auto part = std::ranges::find(parts, part_name, &StaticMeshPart::name);
            if (part == parts.end()) {
                return std::unexpected(ModelImportError{"No model part matches sub-asset " + part_name});
            }
            auto selected = std::move(*part);
            parts.clear();
            parts.push_back(std::move(selected));
        }
        return imported;
    }

    [[nodiscard]] rhi::BufferHandle upload(std::span<const std::byte> bytes, rhi::BufferUsage usage) {
        auto buffer = device.createBuffer({
            .size = bytes.size(),
            .usage = usage,
            .memory = rhi::MemoryType::Upload,
            .debugName = "Scene Preview Mesh",
        });
        if (!buffer) {
            return {};
        }
        if (!device.writeBuffer(*buffer, bytes)) {
            static_cast<void>(device.destroy(*buffer));
            return {};
        }
        return *buffer;
    }

    void load(Guid guid, std::vector<Part> &parts) {
        const auto source = import(guid);
        if (!source) {
            log::warning(k_log_category, "Mesh {}: {}", guid.toString(), source.error().message);
            return;
        }
        for (const auto &part : source->parts) {
            if (part.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }
            const auto vertices = upload(std::as_bytes(std::span(part.vertices)), rhi::BufferUsage::Vertex);
            const auto indices = upload(std::as_bytes(std::span(part.indices)), rhi::BufferUsage::Index);
            if (!indices.isValid() || !vertices.isValid()) {
                static_cast<void>(device.destroy(vertices));
                static_cast<void>(device.destroy(indices));
                log::warning(k_log_category, "Mesh {}: cannot upload {}", guid.toString(), part.name);
                continue;
            }
            const auto color = part.material ? part.material->base_color : std::array{0.61F, 0.68F, 0.77F, 1.0F};
            parts.push_back({vertices, indices, static_cast<std::uint32_t>(part.indices.size()), color});
        }
    }
#endif

    void releaseModels() {
        static_cast<void>(device.waitIdle());
        for (const auto &[guid, parts] : models) {
            for (const auto &part : parts) {
                static_cast<void>(device.destroy(part.vertices));
                static_cast<void>(device.destroy(part.indices));
            }
        }
        models.clear();
    }
};

SceneModels::SceneModels(rhi::Device &device, const std::filesystem::path &shader_directory)
    : m_impl(std::make_unique<Impl>(device)) {
#if defined(STAR_EDITOR_MODEL_IMPORT)
    auto &impl = *m_impl;
    impl.vertex_shader = loadShader(device, shader_directory / "scene-model-vertex.spv", rhi::ShaderStage::Vertex);
    impl.fragment_shader =
        loadShader(device, shader_directory / "scene-model-fragment.spv", rhi::ShaderStage::Fragment);
    const auto layout = device.createPipelineLayout({
        .bind_groups = {},
        .push_constant_size = sizeof(DrawConstants),
        .debugName = "Scene Model Layout",
    });
    if (!layout) {
        return;
    }
    impl.layout = *layout;
    const std::array attributes{
        rhi::VertexAttribute{0, rhi::VertexFormat::Float3, offsetof(StaticMeshVertex, position)},
        rhi::VertexAttribute{1, rhi::VertexFormat::Float3, offsetof(StaticMeshVertex, normal)},
    };
    const std::array vertices{rhi::VertexBufferLayout{sizeof(StaticMeshVertex), attributes}};
    const std::array formats{rhi::TextureFormat::Rgba8Unorm};
    const auto pipeline = device.createGraphicsPipeline({
        .layout = impl.layout,
        .vertex_shader = impl.vertex_shader,
        .fragment_shader = impl.fragment_shader,
        .vertex_buffers = vertices,
        .depth_test = true,
        .depth_write = true,
        .color_formats = formats,
        .has_depth = true,
        .debugName = "Scene Model Preview",
    });
    if (pipeline) {
        impl.pipeline = *pipeline;
    } else {
        log::warning(k_log_category, "Scene model preview pipeline unavailable");
    }
#else
    static_cast<void>(shader_directory);
#endif
}

SceneModels::~SceneModels() {
    auto &impl = *m_impl;
    impl.releaseModels();
    static_cast<void>(impl.device.destroy(impl.pipeline));
    static_cast<void>(impl.device.destroy(impl.layout));
    static_cast<void>(impl.device.destroy(impl.vertex_shader));
    static_cast<void>(impl.device.destroy(impl.fragment_shader));
}

bool SceneModels::isReady() const noexcept { return m_impl->pipeline.isValid(); }

void SceneModels::setProjectRoot(const std::filesystem::path &root) {
    if (m_impl->root != root) {
        m_impl->root = root;
        refresh();
    }
}

void SceneModels::refresh() {
    m_impl->releaseModels();
#if defined(STAR_EDITOR_MODEL_IMPORT)
    m_impl->assets = {};
    m_impl->scanned = false;
#endif
}

void SceneModels::draw(rhi::CommandList &commands, std::span<const SceneModelInstance> instances) {
#if defined(STAR_EDITOR_MODEL_IMPORT)
    if (instances.empty() || !isReady()) {
        return;
    }
    auto &impl = *m_impl;
    commands.bindGraphicsPipeline(impl.pipeline, {});
    for (const auto &instance : instances) {
        auto [entry, inserted] = impl.models.try_emplace(instance.model);
        if (inserted) {
            impl.load(instance.model, entry->second);
        }
        for (const auto &part : entry->second) {
            const DrawConstants constants{instance.clip_from_model, part.color};
            commands.pushConstants(&constants, sizeof(constants));
            commands.bindVertexBuffer(part.vertices);
            commands.bindIndexBuffer(part.indices);
            commands.drawIndexed(part.count);
        }
    }
#else
    static_cast<void>(commands);
    static_cast<void>(instances);
#endif
}

} // namespace star::editor
