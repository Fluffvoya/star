#include "asset-pipeline/derived_data_cache.hpp"
#include "test_support.hpp"

#include <array>
#include <string_view>

namespace {

using namespace star::asset_pipeline;

AssetMeta exampleMeta() {
    AssetMeta meta;
    meta.guid = star::Guid::generate();
    meta.importer = "star::TestImporter";
    meta.importer_version = 1;
    return meta;
}

std::span<const std::byte> bytesOf(std::string_view text) { return std::as_bytes(std::span(text)); }

struct CacheDirectory {
    std::filesystem::path root = std::filesystem::temp_directory_path() / star::Guid::generate().toString();

    ~CacheDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

} // namespace

STAR_TEST(derived_data_key_changes_with_each_import_input) {
    const auto meta = exampleMeta();
    const auto source = bytesOf("asset contents");
    const auto key = makeDerivedDataKey(source, meta, "linux");
    STAR_CHECK(key.has_value());
    STAR_CHECK(makeDerivedDataKey(source, meta, "linux").value() == *key);
    STAR_CHECK(makeDerivedDataKey(bytesOf("new contents"), meta, "linux").value() != *key);
    STAR_CHECK(makeDerivedDataKey(source, meta, "windows").value() != *key);
    STAR_CHECK(!makeDerivedDataKey(source, meta, "").has_value());

    auto updated = meta;
    ++updated.importer_version;
    STAR_CHECK(makeDerivedDataKey(source, updated, "linux").value() != *key);

    updated = meta;
    updated.settings["Option"] = star::serialization::ArchiveValue{star::serialization::PlainScalar{"true"}};
    STAR_CHECK(makeDerivedDataKey(source, updated, "linux").value() != *key);

    const std::array<DerivedDataKey, 1> dependencies{*key};
    STAR_CHECK(makeDerivedDataKey(source, meta, "linux", dependencies).value() != *key);
}

STAR_TEST(derived_data_cache_distinguishes_a_miss_from_an_empty_payload) {
    CacheDirectory directory;
    const auto data = bytesOf("imported payload");
    const auto key = makeDerivedDataKey(data, exampleMeta(), "linux").value();
    const DerivedDataCache cache(directory.root / "Intermediate" / "DDC");

    const auto miss = cache.read(key);
    STAR_CHECK(miss.has_value());
    STAR_CHECK(!miss->has_value());

    STAR_CHECK(cache.store(key, data).has_value());
    const auto hit = cache.read(key);
    STAR_CHECK(hit.has_value());
    STAR_CHECK(hit->value() == std::vector<std::byte>(data.begin(), data.end()));

    STAR_CHECK(cache.store(key, {}).has_value());
    const auto empty = cache.read(key);
    STAR_CHECK(empty.has_value());
    STAR_CHECK(empty->value().empty());
}

STAR_TEST(derived_data_cache_reports_a_file_where_the_cache_directory_is_expected) {
    CacheDirectory directory;
    const auto data = bytesOf("payload");
    const auto key = makeDerivedDataKey(data, exampleMeta(), "linux").value();
    const DerivedDataCache cache(directory.root);
    STAR_CHECK(cache.store(key, data).has_value());

    const auto entry = std::filesystem::directory_iterator(directory.root)->path();
    const DerivedDataCache blocked(entry);
    const auto write = blocked.store(key, data);
    STAR_CHECK(!write.has_value());
    STAR_CHECK(write.error().code == DerivedDataErrorCode::IoFailure);
    const auto read = blocked.read(key);
    STAR_CHECK(!read.has_value());
    STAR_CHECK(read.error().code == DerivedDataErrorCode::IoFailure);
}
