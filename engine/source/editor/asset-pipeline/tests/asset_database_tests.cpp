#include "asset-pipeline/asset_database.hpp"
#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

using namespace star::asset_pipeline;
namespace fs = std::filesystem;

constexpr std::string_view meta_yaml = R"(
StarFile: Meta
Guid: 9b2d4c6e8f0a1b3c5d7e9f1a2b3c4d5e
Importer: star::GltfImporter
ImporterVersion: 4
Settings:
  Scale: 0.01
EditorLabel: "Test"
SubAssets:
  - {Name: Hero, Type: star::Mesh, Guid: 1f2e3d4c5b6a79881f2e3d4c5b6a7988, FutureField: true}
)";

struct ContentDirectory {
    fs::path root = fs::temp_directory_path() / ("star-asset-tests-" + star::Guid::generate().toString());

    ContentDirectory() { STAR_CHECK(fs::create_directory(root)); }
    ~ContentDirectory() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    void write(const fs::path &name, std::string_view contents) const {
        std::ofstream output(root / name, std::ios::binary);
        output << contents;
        output.close();
        STAR_CHECK(output.good());
    }
};

} // namespace

STAR_TEST(asset_meta_round_trips_settings_sub_assets_and_unknown_fields) {
    const auto meta = readAssetMeta(meta_yaml);
    STAR_CHECK(meta.has_value());
    STAR_CHECK(meta->guid.isValid());
    STAR_CHECK(meta->importer == "star::GltfImporter");
    STAR_CHECK(meta->importer_version == 4);
    STAR_CHECK(meta->settings.contains("Scale"));
    STAR_CHECK(meta->extensions.contains("EditorLabel"));
    STAR_CHECK(meta->sub_assets.size() == 1);
    STAR_CHECK(meta->sub_assets.front().extensions.contains("FutureField"));

    const auto yaml = writeAssetMeta(*meta);
    STAR_CHECK(yaml.has_value());
    const auto round_trip = readAssetMeta(*yaml);
    STAR_CHECK(round_trip.has_value());
    STAR_CHECK(*round_trip == *meta);

    auto invalid = *meta;
    invalid.sub_assets.front().guid = invalid.guid;
    const auto duplicate = writeAssetMeta(invalid);
    STAR_CHECK(!duplicate.has_value());
    STAR_CHECK(duplicate.error().code == AssetDatabaseErrorCode::DuplicateGuid);
    STAR_CHECK(!readAssetMeta("StarFile: Scene\n").has_value());
}

STAR_TEST(asset_database_keeps_guids_when_source_and_meta_move_together) {
    ContentDirectory content;
    content.write("hero.glb", "asset");
    content.write("hero.glb.meta", meta_yaml);

    AssetDatabase database;
    STAR_CHECK(database.scan(content.root).has_value());
    const auto *indexed = database.findMeta("hero.glb");
    STAR_CHECK(indexed != nullptr);
    const auto guid = indexed->guid;
    const auto sub_guid = indexed->sub_assets.at(0).guid;
    STAR_CHECK(database.findAsset(guid) != nullptr);
    STAR_CHECK(!database.findAsset(guid)->sub_asset_index);
    STAR_CHECK(database.findAsset(sub_guid) != nullptr);
    STAR_CHECK(database.findAsset(sub_guid)->sub_asset_index == 0);

    fs::create_directory(content.root / "models");
    fs::rename(content.root / "hero.glb", content.root / "models/renamed.glb");
    fs::rename(content.root / "hero.glb.meta", content.root / "models/renamed.glb.meta");
    STAR_CHECK(database.scan(content.root).has_value());
    STAR_CHECK(database.findMeta("hero.glb") == nullptr);
    STAR_CHECK(database.findAsset(guid) != nullptr);
    STAR_CHECK(database.findAsset(guid)->source == fs::path("models/renamed.glb"));
    STAR_CHECK(database.findAsset(sub_guid) != nullptr);
    STAR_CHECK(database.findAsset(sub_guid)->source == fs::path("models/renamed.glb"));
}

STAR_TEST(asset_database_failed_scan_preserves_previous_snapshot) {
    ContentDirectory content;
    content.write("hero.glb", "asset");
    content.write("hero.glb.meta", meta_yaml);
    AssetDatabase database;
    STAR_CHECK(database.scan(content.root).has_value());
    const auto *indexed = database.findMeta("hero.glb");

    content.write("duplicate.glb.meta", meta_yaml);
    const auto orphan = database.scan(content.root);
    STAR_CHECK(!orphan.has_value());
    STAR_CHECK(orphan.error().code == AssetDatabaseErrorCode::MissingSource);
    STAR_CHECK(database.findMeta("hero.glb") == indexed);

    content.write("duplicate.glb", "asset");
    const auto duplicate = database.scan(content.root);
    STAR_CHECK(!duplicate.has_value());
    STAR_CHECK(duplicate.error().code == AssetDatabaseErrorCode::DuplicateGuid);
    STAR_CHECK(database.findMeta("hero.glb") == indexed);
    STAR_CHECK(database.findMeta("duplicate.glb") == nullptr);

    content.write("duplicate.glb.meta", "");
    const auto invalid = database.scan(content.root);
    STAR_CHECK(!invalid.has_value());
    STAR_CHECK(invalid.error().code == AssetDatabaseErrorCode::InvalidMeta);
    STAR_CHECK(database.findMeta("hero.glb") == indexed);
}
