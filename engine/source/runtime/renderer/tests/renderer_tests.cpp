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
    const auto empty = ForwardRenderer::create({}, {});
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
    material.variant.state.blend = MaterialBlendMode::Transparent;
    const auto unsupported = ForwardRenderer::create(simpleModel(), {}, material);
    STAR_CHECK(!unsupported.has_value());
    STAR_CHECK(unsupported.error().code == ForwardRenderErrorCode::Unsupported);
}
