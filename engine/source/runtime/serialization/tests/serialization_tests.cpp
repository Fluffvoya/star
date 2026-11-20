#include "serialization/archive.hpp"

#include "test_support.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using namespace star::reflect;
using namespace star::serialization;

struct Nested {
    bool enabled = true;
    friend bool operator==(const Nested &, const Nested &) = default;
};

constexpr PropertyInfo nested_properties[] = {makeProperty<Nested, bool, &Nested::enabled>("enabled")};
constexpr TypeInfo nested_type{makeTypeId("tests::Nested"),
                               "tests::Nested",
                               TypeKind::Struct,
                               sizeof(Nested),
                               alignof(Nested),
                               nested_properties,
                               makeTypeOps<Nested>(),
                               7,
                               1,
                               nullptr};

struct Document {
    std::int8_t count = 4;
    std::uint64_t mask = 1;
    float ratio = 1.25f;
    std::string title = "default";
    Nested nested{};
    std::int32_t scratch = 9;
    friend bool operator==(const Document &, const Document &) = default;
};

constexpr PropertyInfo properties[] = {
    makeProperty<Document, std::int8_t, &Document::count>("count", PropertyFlags::None, nullptr, "old_count"),
    makeProperty<Document, std::uint64_t, &Document::mask>("mask"),
    makeProperty<Document, float, &Document::ratio>("ratio"),
    makeProperty<Document, std::string, &Document::title>("title"),
    makeProperty<Document, Nested, &Document::nested>("nested", PropertyFlags::None, &nested_type),
    makeProperty<Document, std::int32_t, &Document::scratch>("scratch", PropertyFlags::Transient),
};
constexpr TypeInfo type{makeTypeId("tests::Document"),
                        "tests::Document",
                        TypeKind::Struct,
                        sizeof(Document),
                        alignof(Document),
                        properties,
                        makeTypeOps<Document>(),
                        7,
                        3,
                        nullptr,
                        "tests::OldDocument"};

STAR_TEST(serializationRoundTripsBothFormatsAndKeepsUnknownData) {
    const auto source = readYaml(R"yaml(
Type: tests::Document
Version: 3
Properties:
  count: -128
  mask: 18446744073709551615
  ratio: 0.00000425
  title: "line\n\"quote\"\\slash\x00"
  scratch: 987
  nested: {enabled: false, unknown: [null, 42, "42", {key: true}]}
  future: {"key: with punctuation": ["", false, .nan, 0xCAFE, null, "null"]}
)yaml");
    STAR_CHECK(source.has_value());

    Document loaded;
    const auto assigned = applyObject(*source, type, &loaded);
    STAR_CHECK(assigned.has_value());
    STAR_CHECK(assigned->diagnostics.size() == 2);
    STAR_CHECK(loaded.count == std::numeric_limits<std::int8_t>::min());
    STAR_CHECK(loaded.mask == std::numeric_limits<std::uint64_t>::max());
    STAR_CHECK(loaded.ratio == 0.00000425f);
    STAR_CHECK(loaded.title == std::string("line\n\"quote\"\\slash\0", 19));
    STAR_CHECK(!loaded.nested.enabled);
    STAR_CHECK(loaded.scratch == 9);

    loaded.nested.enabled = true;
    const auto saved = captureObject(type, &loaded, assigned->extensions);
    STAR_CHECK(saved.has_value());
    STAR_CHECK(!saved->fields.contains("scratch"));

    const auto encoded = writeBinary(*saved);
    STAR_CHECK(encoded.has_value());
    const auto decoded = readBinary(*encoded);
    STAR_CHECK(decoded == *saved);

    const auto emitted = writeYaml(*decoded);
    STAR_CHECK(emitted.has_value());
    const auto reread = readYaml(*emitted);
    STAR_CHECK(reread == *saved);

    Document restored;
    const auto reassigned = applyObject(*reread, type, &restored);
    STAR_CHECK(reassigned.has_value());
    STAR_CHECK(restored == loaded);
    STAR_CHECK(reassigned->extensions == assigned->extensions);
}

bool renameTitle(ArchiveValue::Mapping &fields, std::uint32_t from, std::uint32_t to) {
    if (from != 1 || to != 3 || !fields.contains("caption")) {
        return false;
    }
    auto old_title = fields.extract("caption");
    old_title.key() = "title";
    return fields.insert(std::move(old_title)).inserted;
}

STAR_TEST(serializationMigratesFormerTypeAndFieldNames) {
    const auto source = readYaml("Type: tests::OldDocument\nProperties: {old_count: 0x1f, caption: migrated}");
    STAR_CHECK(source.has_value());
    Document loaded;
    const auto result = applyObject(*source, type, &loaded, LoadPolicy::Strict, &renameTitle);
    STAR_CHECK(result.has_value());
    STAR_CHECK(loaded.count == 31);
    STAR_CHECK(loaded.title == "migrated");
    const auto saved = captureObject(type, &loaded);
    STAR_CHECK(saved.has_value());
    STAR_CHECK(saved->type_name == type.name);
    STAR_CHECK(saved->version == type.version);
    STAR_CHECK(saved->fields.contains("count"));
    STAR_CHECK(!saved->fields.contains("old_count"));

    const auto missing_title = readYaml("Type: tests::OldDocument\nProperties: {}");
    STAR_CHECK(missing_title.has_value());
    const auto failed = applyObject(*missing_title, type, &loaded, LoadPolicy::Strict, &renameTitle);
    STAR_CHECK(!failed.has_value());
    STAR_CHECK(failed.error().code == ArchiveErrorCode::MigrationFailed);
    STAR_CHECK(loaded.title == "migrated");
}

STAR_TEST(serializationPreservesUnconvertibleFieldsAndRollsBackStrictLoads) {
    const auto source = readYaml("Type: tests::Document\nProperties: {count: 19, ratio: [1, 2]}");
    STAR_CHECK(source.has_value());
    Document loaded;
    const auto strict = applyObject(*source, type, &loaded, LoadPolicy::Strict);
    STAR_CHECK(!strict.has_value());
    STAR_CHECK(strict.error().code == ArchiveErrorCode::TypeMismatch);
    STAR_CHECK(strict.error().path == "/ratio");
    STAR_CHECK(loaded == Document{});

    const auto preserving = applyObject(*source, type, &loaded);
    STAR_CHECK(preserving.has_value());
    STAR_CHECK(loaded.count == 19);
    const auto saved = captureObject(type, &loaded, preserving->extensions);
    STAR_CHECK(saved.has_value());
    STAR_CHECK(saved->fields.at("ratio") == source->fields.at("ratio"));

    const auto overflow = readYaml("Type: tests::Document\nProperties: {count: 128}");
    STAR_CHECK(overflow.has_value());
    STAR_CHECK(!applyObject(*overflow, type, &loaded, LoadPolicy::Strict));
    STAR_CHECK(loaded.count == 19);
}

STAR_TEST(serializationRejectsInvalidInputsAndKeepsUnknownTypesAsDocuments) {
    STAR_CHECK(!readYaml("Type: X\nProperties: {a: 1, a: 2}"));
    STAR_CHECK(!readYaml("Type: X\nProperties: [1,"));
    STAR_CHECK(!readYaml("Type: X\nProperties: {a: &a 1, b: *a}"));
    STAR_CHECK(!readYaml("Type: X\nVersion: 0\nProperties: {}"));

    const auto unknown = readYaml("Type: unavailable::Type\nVersion: 42\nProperties: {a: [true, 'true']}");
    STAR_CHECK(unknown.has_value());
    auto binary = writeBinary(*unknown);
    STAR_CHECK(binary.has_value());
    STAR_CHECK(readBinary(*binary) == *unknown);
    STAR_CHECK(!readBinary(std::span(*binary).first(binary->size() - 1)));
    binary->push_back(std::byte{0});
    STAR_CHECK(!readBinary(*binary));
    binary->at(4) = std::byte{2};
    const auto unsupported = readBinary(*binary);
    STAR_CHECK(!unsupported.has_value());
    STAR_CHECK(unsupported.error().code == ArchiveErrorCode::UnsupportedVersion);

    Document object;
    const auto mismatch = applyObject(*unknown, type, &object);
    STAR_CHECK(!mismatch.has_value());
    STAR_CHECK(mismatch.error().code == ArchiveErrorCode::TypeMismatch);
    const auto future = readYaml("Type: tests::Document\nVersion: 42\nProperties: {}");
    STAR_CHECK(future.has_value());
    const auto future_result = applyObject(*future, type, &object);
    STAR_CHECK(!future_result.has_value());
    STAR_CHECK(future_result.error().code == ArchiveErrorCode::UnsupportedVersion);
}

STAR_TEST(serializationRoundTripsEmptyDocumentsAndNonfiniteFloats) {
    const ArchiveDocument empty{"unknown::Empty", 1, {}};
    const auto yaml = writeYaml(empty);
    STAR_CHECK(yaml.has_value());
    STAR_CHECK(readYaml(*yaml) == empty);

    Document input;
    input.ratio = -std::numeric_limits<float>::infinity();
    const auto encoded = captureObject(type, &input);
    STAR_CHECK(encoded.has_value());
    const auto binary = writeBinary(*encoded);
    STAR_CHECK(binary.has_value());
    const auto decoded = readBinary(*binary);
    STAR_CHECK(decoded.has_value());
    Document object;
    STAR_CHECK(applyObject(*decoded, type, &object).has_value());
    STAR_CHECK(std::isinf(object.ratio));
    STAR_CHECK(std::signbit(object.ratio));
}

} // namespace
