#include "renderer/forward_renderer.hpp"
#include "test_support.hpp"

namespace {

using namespace star;
using namespace star::renderer;

StaticModelAsset simpleModel() {
    StaticModelAsset model;
    auto &part = model.parts.emplace_back();
    part.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}}, {{1, -1, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
    part.indices = {0, 1, 2};
    return model;
}

} // namespace

STAR_TEST(forward_renderer_rejects_invalid_input_before_creating_a_backend) {
    const auto empty = ForwardRenderer::create(StaticModelAsset{}, {});
    STAR_CHECK(!empty.has_value());
    STAR_CHECK(empty.error().code == ForwardRenderErrorCode::InvalidInput);

    auto model = simpleModel();
    model.parts.front().indices.back() = 3;
    const auto invalid_indices = ForwardRenderer::create(model, {});
    STAR_CHECK(!invalid_indices.has_value());
    STAR_CHECK(invalid_indices.error().code == ForwardRenderErrorCode::InvalidInput);

    model = simpleModel();
    model.textures.push_back({TextureColorSpace::Color, {{2, 2, {0, 0, 0, 255}}}});
    const auto invalid_texture = ForwardRenderer::create(model, {});
    STAR_CHECK(!invalid_texture.has_value());
    STAR_CHECK(invalid_texture.error().code == ForwardRenderErrorCode::InvalidInput);
}

STAR_TEST(forward_renderer_rejects_unregistered_material_variants) {
    auto material = defaultForwardMaterial();
    material.variant.shader = Guid::generate();
    const auto unregistered = ForwardRenderer::create(simpleModel(), {}, material);
    STAR_CHECK(!unregistered.has_value());
    STAR_CHECK(unregistered.error().code == ForwardRenderErrorCode::Unsupported);

    material = defaultForwardMaterial();
    material.variant.state.depth_test = false;
    const auto no_depth = ForwardRenderer::create(simpleModel(), {}, material);
    STAR_CHECK(!no_depth.has_value());
    STAR_CHECK(no_depth.error().code == ForwardRenderErrorCode::Unsupported);
}

STAR_TEST(forward_renderer_validates_scene_snapshots_before_creating_a_backend) {
    RenderScene scene;
    const auto empty = ForwardRenderer::create(scene, {});
    STAR_CHECK(!empty && empty.error().code == ForwardRenderErrorCode::InvalidInput);

    scene.model = simpleModel();
    scene.shadow_casters.assign(scene.model.parts.size(), 1);
    scene.cameras.push_back({math::Mat4::identity(), 0.9F, 0.1F, 10.0F});
    scene.cameras.front().vertical_fov = 0.0F;
    const auto invalid_camera = ForwardRenderer::create(scene, {});
    STAR_CHECK(!invalid_camera && invalid_camera.error().code == ForwardRenderErrorCode::InvalidInput);

    scene.cameras.front().vertical_fov = 0.9F;
    ForwardRenderOptions options;
    options.camera_index = 3;
    const auto missing_camera = ForwardRenderer::create(scene, options);
    STAR_CHECK(!missing_camera && missing_camera.error().code == ForwardRenderErrorCode::InvalidInput);

    scene.point_lights.assign(kMaxRenderPointLights + 1, RenderPointLight{});
    const auto too_many_lights = ForwardRenderer::create(scene, {});
    STAR_CHECK(!too_many_lights && too_many_lights.error().code == ForwardRenderErrorCode::InvalidInput);

    scene.point_lights.clear();
    scene.directional_light = RenderDirectionalLight{{}, {1.0F, 1.0F, 1.0F}, 1.0F};
    const auto invalid_sun = ForwardRenderer::create(scene, {});
    STAR_CHECK(!invalid_sun && invalid_sun.error().code == ForwardRenderErrorCode::InvalidInput);
}
