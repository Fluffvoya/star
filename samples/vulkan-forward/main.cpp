#include "asset-pipeline/model_importer.hpp"
#include "asset/asset_manager.hpp"
#include "renderer/forward_renderer.hpp"
#include "sample_material.hpp"

#include <charconv>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace star::asset_pipeline;

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
            ? importGltfTexturedModel(bytes, [&](std::string_view name) { return readFile(path.parent_path() / name); })
            : importStaticModel(bytes, extension == ".fbx" ? StaticModelFormat::Fbx : StaticModelFormat::Obj);
    if (!model) {
        throw std::runtime_error(model.error().message);
    }
    for (auto &part : model->parts) {
        if (part.material) {
            continue;
        }
        if (part.name == "Copper") {
            part.material = {{0.95F, 0.56F, 0.37F, 1}, 0.85F, 0.26F, {}, {}};
        } else if (part.name == "Ceramic") {
            part.material = {{0.10F, 0.58F, 0.55F, 1}, 0.0F, 0.34F, {}, {}};
        } else if (part.name == "Paint") {
            part.material = {{0.60F, 0.20F, 0.09F, 1}, 0.0F, 0.7F, {}, {}};
        }
    }
    return std::move(*model);
}

using SampleModel = std::variant<ImportedStaticModel, star::AssetHandle<star::StaticModelAsset>>;

SampleModel loadModel(const std::filesystem::path &path) {
    if (path.extension() != ".smdl") {
        return importModel(path);
    }
    star::AssetManager manager;
    const star::AssetRef<star::StaticModelAsset> reference{star::Guid::generate()};
    if (!manager.registerAsset<star::StaticModelAsset>(reference.guid, path)) {
        throw std::runtime_error("Cannot register derived model " + path.string());
    }
    auto loaded = reference.load(manager);
    if (!loaded) {
        throw std::runtime_error("Cannot load derived model " + path.string());
    }
    return std::move(*loaded);
}

void saveCapture(const star::renderer::ForwardImage &image, const std::filesystem::path &path) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        output.write(reinterpret_cast<const char *>(image.rgba.data() + i), 3);
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        bool front_view = false;
        auto material = star::renderer::defaultForwardMaterial();
        for (int index = 4; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--front") {
                front_view = true;
            } else if (option.starts_with("--material=") && option.size() > 11) {
                material = star::sample::loadSampleMaterial(option.substr(11));
            } else {
                throw std::runtime_error(
                    "Usage: star-vulkan-forward [model] [capture.ppm] [frames] [--front] [--material=path]");
            }
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
        const auto loaded = loadModel(source);
        const auto *source_model = std::get_if<ImportedStaticModel>(&loaded);
        const auto &model =
            source_model ? *source_model : *std::get<star::AssetHandle<star::StaticModelAsset>>(loaded).get();
        auto created = star::renderer::ForwardRenderer::create(model, {assets, front_view, true}, material);
        if (!created) {
            throw std::runtime_error(created.error().message);
        }
        auto &renderer = **created;
        const auto render = [&] {
            const auto duration = renderer.render();
            if (!duration) {
                throw std::runtime_error(duration.error().message);
            }
            return *duration;
        };
        const auto warmup_ms = render();
        double total_ms = 0.0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            total_ms += render();
        }
        const auto image = renderer.capture();
        if (!image) {
            throw std::runtime_error(image.error().message);
        }
        saveCapture(*image, capture);
        std::printf("Vulkan %s, validation enabled, %ux%u, %zu mesh parts\n", renderer.getDeviceName().data(),
                    image->width, image->height, model.parts.size());
        if (total_ms > 0.0) {
            std::printf("%u frames, %.3f ms average GPU time, %.3f ms warmup\n", frames, total_ms / frames, warmup_ms);
        }
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
