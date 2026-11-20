#include "fixtures/reflected_types.hpp"
#include "generated_tests.gen.hpp"
#include "serialization/archive.hpp"
#include "test_support.hpp"

void star::test::CustomDocument::serialize(serialization::Archive &archive) { archive.value("payload", value); }

STAR_TEST(generatedReflectionRegistersAndSerializesPrivateFieldsAndEnums) {
    using namespace star::serialization;
    star::reflect::TypeRegistry registry;
    const auto types = star::generated::generated_tests::types();
    STAR_CHECK(types.size() == 4);
    STAR_CHECK(registry.registerModule(37, types));
    const auto *type = registry.find("star::test::Widget");
    STAR_CHECK(type != nullptr);
    STAR_CHECK(type == registry.find("star::test::OldWidget"));
    STAR_CHECK(type->version == 3);
    STAR_CHECK(type->properties.size() == 6);
    STAR_CHECK(type->base == registry.find("star::test::Settings"));
    STAR_CHECK(type->properties.front().metadata.find("Range") != std::string_view::npos);
    STAR_CHECK(type->functions.size() == 1);
    STAR_CHECK(type->functions.front().metadata == "CallInEditor");

    star::test::Widget object;
    const auto yaml = readYaml(R"yaml(
Type: star::test::OldWidget
Properties:
  enabled: false
  old_count: 9
  m_visibility: Hidden
  m_history: [Visible, Hidden]
  m_weights: [4, 7]
  m_named: {second: 2, first: 1}
)yaml");
    STAR_CHECK(yaml.has_value());
    STAR_CHECK(applyObject(*yaml, *type, &object, LoadPolicy::Strict).has_value());
    STAR_CHECK(object.count() == 9);
    STAR_CHECK(object.visibility() == star::test::Visibility::Hidden);
    STAR_CHECK(!object.enabled);
    STAR_CHECK(object.history().size() == 2);
    STAR_CHECK(object.history().back() == star::test::Visibility::Hidden);
    const auto snapshot = captureObject(*type, &object);
    STAR_CHECK(snapshot.has_value());
    STAR_CHECK(snapshot->fields.at("enabled") == ArchiveValue{PlainScalar{"false"}});
    const auto binary = writeBinary(*snapshot);
    STAR_CHECK(binary.has_value());
    const auto restored = readBinary(*binary);
    STAR_CHECK(restored == *snapshot);

    const auto invalid = readYaml("Type: star::test::Widget\nProperties: {m_history: [Hidden, Invalid]}");
    STAR_CHECK(invalid.has_value());
    const auto preserved = applyObject(*invalid, *type, &object);
    STAR_CHECK(preserved.has_value());
    STAR_CHECK(preserved->diagnostics.size() == 1);
    STAR_CHECK(preserved->diagnostics.front().path == "/m_history/1");
    STAR_CHECK(object.history().front() == star::test::Visibility::Visible);
    const auto resaved = captureObject(*type, &object, preserved->extensions);
    STAR_CHECK(resaved.has_value());
    STAR_CHECK(resaved->fields.at("m_history") == invalid->fields.at("m_history"));

    STAR_CHECK(type->functions.front().invoke(&object));
    STAR_CHECK(object.count() == 0);
    registry.unregisterModule(37);
    STAR_CHECK(registry.find("star::test::Widget") == nullptr);
    STAR_CHECK(registry.find("star::test::OldWidget") == nullptr);
}

STAR_TEST(generatedCustomSerializationCanOmitDefaultsAndPreserveUnknownFields) {
    using namespace star::serialization;
    star::reflect::TypeRegistry registry;
    STAR_CHECK(registry.registerModule(37, star::generated::generated_tests::types()));
    const auto *type = registry.find("star::test::CustomDocument");
    STAR_CHECK(type != nullptr);

    star::test::CustomDocument object;
    const auto compact = captureObject(*type, &object, {}, CapturePolicy::OmitDefaults);
    STAR_CHECK(compact.has_value());
    STAR_CHECK(compact->fields.empty());

    const auto yaml = readYaml("Type: star::test::CustomDocument\nProperties: {payload: 19, newer: [true]}");
    STAR_CHECK(yaml.has_value());
    const auto strict = applyObject(*yaml, *type, &object, LoadPolicy::Strict);
    STAR_CHECK(!strict.has_value());
    STAR_CHECK(strict.error().path == "/newer");
    STAR_CHECK(object.value == 5);

    const auto loaded = applyObject(*yaml, *type, &object);
    STAR_CHECK(loaded.has_value());
    STAR_CHECK(object.value == 19);
    const auto saved = captureObject(*type, &object, loaded->extensions, CapturePolicy::OmitDefaults);
    STAR_CHECK(saved == *yaml);
    const auto binary = writeBinary(*saved);
    STAR_CHECK(binary.has_value());
    STAR_CHECK(readBinary(*binary) == *yaml);
}
