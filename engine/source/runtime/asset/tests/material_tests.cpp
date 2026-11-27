#include "asset/material.hpp"
#include "test_support.hpp"

namespace {

using namespace star;
using namespace star::serialization;

constexpr std::string_view material_yaml = R"(
Type: star::Material
Version: 1
Properties:
  Shader: 9b2d4c6e8f0a1b3c5d7e9f1a2b3c4d5e
  Parameters:
    BaseColor: [1, 0.5, 0, 1]
    Roughness: 0.5
  RenderState:
    Blend: Masked
    DoubleSided: true
    FutureSetting: keep
  FutureField: "keep"
)";

} // namespace

STAR_TEST(material_preserves_source_fields_and_round_trips_binary) {
    const auto document = readYaml(material_yaml);
    STAR_CHECK(document.has_value());
    const auto material = readMaterial(*document);
    STAR_CHECK(material.has_value());
    STAR_CHECK(material->state.blend == MaterialBlendMode::Masked);
    STAR_CHECK(material->state.double_sided);
    STAR_CHECK(material->state.depth_test);
    STAR_CHECK(material->parameters.contains("BaseColor"));
    STAR_CHECK(material->extensions.contains("FutureField"));
    STAR_CHECK(material->state_extensions.contains("FutureSetting"));

    const auto strict = readMaterial(*document, LoadPolicy::Strict);
    STAR_CHECK(!strict.has_value());
    STAR_CHECK(strict.error().path == "FutureField");
    auto clean = *material;
    clean.extensions.clear();
    clean.state_extensions.clear();
    const auto bytes = writeBinary(writeMaterial(clean).value());
    STAR_CHECK(bytes.has_value());
    const auto read = readMaterial(readBinary(*bytes).value(), LoadPolicy::Strict);
    STAR_CHECK(read.has_value());
    STAR_CHECK(*read == clean);
}

STAR_TEST(material_instance_overrides_parameters_and_preserves_variant_key) {
    const auto material = readMaterial(readYaml(material_yaml).value()).value();
    const auto parent_guid = Guid::generate();
    MaterialInstanceAsset instance;
    instance.parent = parent_guid;
    instance.overrides["Roughness"] = ArchiveValue{PlainScalar{"0.8"}};
    const auto document = writeMaterialInstance(instance).value();
    const auto reloaded = readMaterialInstance(readYaml(writeYaml(document).value()).value()).value();
    STAR_CHECK(reloaded == instance);

    auto resolved = resolveMaterialInstance(parent_guid, material, reloaded);
    STAR_CHECK(resolved.has_value());
    STAR_CHECK(resolved->getVariantKey() == material.getVariantKey());
    STAR_CHECK(resolved->parameters.at("Roughness") == instance.overrides.at("Roughness"));
    STAR_CHECK(resolved->parameters.at("BaseColor") == material.parameters.at("BaseColor"));
    STAR_CHECK(!resolveMaterialInstance(Guid::generate(), material, reloaded).has_value());
    resolved->state.double_sided = false;
    STAR_CHECK(resolved->getVariantKey() != material.getVariantKey());
}

STAR_TEST(material_rejects_invalid_version_and_render_state) {
    auto document = readYaml(material_yaml).value();
    document.version = 2;
    auto result = readMaterial(document);
    STAR_CHECK(!result.has_value());
    STAR_CHECK(result.error().code == ArchiveErrorCode::UnsupportedVersion);
    document.version = 1;
    std::get<ArchiveValue::Mapping>(document.fields["RenderState"].value)["DepthTest"] =
        ArchiveValue{std::string("yes")};
    result = readMaterial(document);
    STAR_CHECK(!result.has_value());
    STAR_CHECK(result.error().path == "RenderState.DepthTest");
}
