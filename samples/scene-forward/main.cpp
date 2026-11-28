// Renders a scene extracted from World storage. The sample builds a small scene, bakes a render
// snapshot, then animates the camera and lighting through the per-frame renderer API.
#include "asset/asset_manager.hpp"
#include "renderer/forward_renderer.hpp"
#include "scene/scene.hpp"
#include "scene/static_render_snapshot.hpp"
#include "scene/world.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace star;

struct Face {
    math::Vec3 normal;
    std::array<math::Vec3, 4> corners;
};

const std::array<Face, 6> kFaces{{
    {{1, 0, 0}, {{{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}}}},
    {{-1, 0, 0}, {{{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}}}},
    {{0, 1, 0}, {{{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}}},
    {{0, -1, 0}, {{{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}}},
    {{0, 0, 1}, {{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}}},
    {{0, 0, -1}, {{{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}}},
}};

void addBox(StaticModelAsset &model, std::string name, math::Vec3 center, math::Vec3 half, std::array<float, 4> color,
            MaterialBlendMode blend = MaterialBlendMode::Opaque) {
    auto &part = model.parts.emplace_back();
    part.name = std::move(name);
    part.material = StaticPbrMaterial{color, 0.0F, 0.35F, blend, 0.5F, {}, {}};
    for (const auto &face : kFaces) {
        const auto base = static_cast<std::uint32_t>(part.vertices.size());
        for (const auto &corner : face.corners) {
            const auto position = center + math::Vec3{corner.x * half.x, corner.y * half.y, corner.z * half.z};
            part.vertices.push_back({position, face.normal, {}});
        }
        part.indices.insert(part.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

math::Quaternion rotationTowards(math::Vec3 direction) {
    const auto target = math::normalize(direction);
    const math::Vec3 reference{0.0F, 0.0F, 1.0F};
    const auto dot = math::dot(reference, target);
    if (dot >= 1.0F - 1e-6F) {
        return math::Quaternion::identity();
    }
    if (dot <= -1.0F + 1e-6F) {
        return math::Quaternion::fromAxisAngle({1.0F, 0.0F, 0.0F}, math::pi);
    }
    const auto axis = math::normalize(math::cross(reference, target));
    return math::Quaternion::fromAxisAngle(axis, std::acos(dot));
}

void saveCapture(const renderer::ForwardImage &image, const std::filesystem::path &path) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t index = 0; index < image.rgba.size(); index += 4) {
        output.write(reinterpret_cast<const char *>(image.rgba.data() + index), 3);
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

bool uniformImage(const renderer::ForwardImage &image) {
    for (std::size_t index = 4; index < image.rgba.size(); index += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (image.rgba[index + channel] != image.rgba[channel]) {
                return false;
            }
        }
    }
    return true;
}

struct TemporaryModel {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("star-sample-" + Guid::generate().toString() + ".smdl");
    ~TemporaryModel() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

} // namespace

int main(int argc, char **argv) {
    try {
        const auto assets = std::filesystem::canonical(argv[0]).parent_path();
        const auto capture = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("scene-capture.ppm");
        unsigned frames = 8;
        if (argc > 2) {
            const std::string_view text(argv[2]);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), frames);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || frames == 0) {
                throw std::runtime_error("Frames must be a positive integer");
            }
        }

        StaticModelAsset model;
        addBox(model, "Ground", {0.0F, -0.15F, 0.0F}, {8.0F, 0.15F, 8.0F}, {0.42F, 0.44F, 0.47F, 1.0F});
        addBox(model, "Copper", {0.0F, 0.75F, 0.0F}, {0.75F, 0.75F, 0.75F}, {0.95F, 0.56F, 0.37F, 1.0F});
        addBox(model, "Ceramic", {-2.2F, 0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {0.10F, 0.58F, 0.55F, 1.0F});
        addBox(model, "Paint", {2.3F, 1.0F, -0.4F}, {0.6F, 1.0F, 0.6F}, {0.60F, 0.20F, 0.09F, 1.0F});
        addBox(model, "Glass", {-1.15F, 1.35F, 1.8F}, {0.85F, 0.85F, 0.03F}, {0.62F, 0.82F, 1.0F, 0.35F},
               MaterialBlendMode::Transparent);

        TemporaryModel file;
        const auto bytes = writeModelAsset(model).value();
        {
            std::ofstream output(file.path, std::ios::binary);
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output) {
                throw std::runtime_error("Cannot write " + file.path.string());
            }
        }

        World world;
        auto &scene = *world.createScene().value();
        const auto mesh_id = Guid::generate();

        const auto mesh_object = scene.createObject("Props").value();
        const MeshRenderer mesh{mesh_id, {}, true, 1};
        if (!scene.queueAddComponent(mesh_object, componentTypeId<MeshRenderer>(), &mesh)) {
            throw std::runtime_error("Cannot attach the mesh renderer");
        }

        const auto camera_object = scene.createObject("Camera").value();
        const Camera camera{math::pi / 3.0F, 0.1F, 100.0F};
        if (!scene.queueAddComponent(camera_object, componentTypeId<Camera>(), &camera)) {
            throw std::runtime_error("Cannot attach the camera");
        }
        scene.findComponent<Transform>(camera_object)->rotation =
            rotationTowards(math::normalize(math::Vec3{0.0F, -0.28F, -1.0F}));
        scene.findComponent<Transform>(camera_object)->position = {0.0F, 3.0F, 9.0F};

        const auto sun_object = scene.createObject("Sun").value();
        const Light sun{true, {1.0F, 0.96F, 0.9F}, 3.5F, 1.0F};
        if (!scene.queueAddComponent(sun_object, componentTypeId<Light>(), &sun)) {
            throw std::runtime_error("Cannot attach the sun");
        }
        scene.findComponent<Transform>(sun_object)->rotation =
            rotationTowards(math::normalize(math::Vec3{0.45F, 0.85F, 0.35F}));

        const std::array<Light, 2> lamps{Light{false, {1.0F, 0.55F, 0.2F}, 26.0F, 8.0F},
                                         Light{false, {0.2F, 0.45F, 1.0F}, 20.0F, 7.0F}};
        const std::array<math::Vec3, 2> lamp_positions{{{2.5F, 2.0F, 2.5F}, {-2.5F, 1.5F, 2.0F}}};
        for (std::size_t index = 0; index < lamps.size(); ++index) {
            const auto lamp = scene.createObject("Lamp").value();
            if (!scene.queueAddComponent(lamp, componentTypeId<Light>(), &lamps[index])) {
                throw std::runtime_error("Cannot attach a lamp");
            }
            scene.findComponent<Transform>(lamp)->position = lamp_positions[index];
        }

        if (!scene.commitCommands()) {
            throw std::runtime_error("Cannot commit the sample scene");
        }

        AssetManager asset_manager;
        if (!asset_manager.registerAsset<StaticModelAsset>(mesh_id, file.path)) {
            throw std::runtime_error("Cannot register the sample model");
        }
        const auto snapshot = extractStaticRenderSnapshot(scene, asset_manager);
        if (!snapshot) {
            throw std::runtime_error("Cannot extract the render snapshot");
        }

        auto created = renderer::ForwardRenderer::create(*snapshot, {assets, false, true});
        if (!created) {
            if (created.error().code == renderer::ForwardRenderErrorCode::BackendFailure) {
                std::fprintf(stderr, "Skipping the scene smoke test: %s\n", created.error().message.c_str());
                return 77;
            }
            throw std::runtime_error(created.error().message);
        }
        auto &renderer = **created;

        const math::Vec3 target{0.0F, 1.0F, 0.0F};
        const auto orbit = [&](unsigned frame) {
            const auto angle = static_cast<float>(frame) * 0.05F;
            const auto eye = target + math::Vec3{std::sin(angle) * 7.0F, 2.1F, std::cos(angle) * 7.0F};
            return renderer::makeLookAtCamera(eye, target, {0.0F, 1.0F, 0.0F}, math::pi / 3.0F, 0.1F, 100.0F);
        };

        double total_ms = 0.0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            const auto camera_state = orbit(frame);
            if (const auto applied = renderer.setCamera(camera_state); !applied) {
                throw std::runtime_error(applied.error().message);
            }
            std::array<renderer::RenderPointLight, 2> animated{};
            for (std::size_t index = 0; index < animated.size(); ++index) {
                const auto pulse = 0.75F + 0.25F * std::sin(static_cast<float>(frame) * 0.2F);
                animated[index] = {{lamp_positions[index].x, lamp_positions[index].y, lamp_positions[index].z},
                                   {lamps[index].color.x, lamps[index].color.y, lamps[index].color.z},
                                   lamps[index].intensity * pulse,
                                   lamps[index].range};
            }
            const renderer::RenderDirectionalLight sun_state{{0.45F, 0.85F, 0.35F}, sun.color, sun.intensity};
            if (const auto applied = renderer.setLights(animated, &sun_state); !applied) {
                throw std::runtime_error(applied.error().message);
            }
            const auto duration = renderer.render();
            if (!duration) {
                throw std::runtime_error(duration.error().message);
            }
            if (frame != 0) {
                total_ms += *duration;
            }
        }

        const auto image = renderer.capture();
        if (!image) {
            throw std::runtime_error(image.error().message);
        }
        if (uniformImage(*image)) {
            throw std::runtime_error("The captured frame is uniform");
        }
        saveCapture(*image, capture);
        std::printf("Scene %s, validation enabled, %ux%u, %zu parts, %zu lights\n", renderer.getDeviceName().data(),
                    image->width, image->height, snapshot->model.parts.size(), snapshot->point_lights.size());
        if (frames > 1) {
            std::printf("%u frames, %.3f ms average GPU time\n", frames - 1, total_ms / (frames - 1));
        }
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
