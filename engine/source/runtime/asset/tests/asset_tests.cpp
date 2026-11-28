#include "asset/asset_manager.hpp"
#include "asset/model_asset.hpp"
#include "asset/texture_asset.hpp"
#include "test_support.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

namespace {

using namespace star;

struct AssetFile {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("star-blob-" + Guid::generate().toString());

    ~AssetFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    void write(std::string_view contents) const {
        std::ofstream output(path, std::ios::binary);
        output << contents;
        output.close();
        STAR_CHECK(output.good());
    }
};

std::string_view asText(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

} // namespace

STAR_TEST(asset_manager_publishes_coalesced_async_reads_on_the_owner) {
    AssetFile file;
    file.write("derived data");
    const auto guid = Guid::generate();
    AssetManager manager;
    STAR_CHECK(manager.registerBlob(guid, file.path).has_value());
    STAR_CHECK(manager.registerBlob(guid, file.path).error() == AssetLoadError::DuplicateGuid);

    const auto first = manager.loadAsync(guid);
    const auto second = manager.loadAsync(guid);
    STAR_CHECK(first.has_value());
    STAR_CHECK(second.has_value());
    STAR_CHECK(first->getState() == AssetState::Loading);
    STAR_CHECK(first->getBytes().empty());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (first->getState() == AssetState::Loading && std::chrono::steady_clock::now() < deadline) {
        manager.processCompletedLoads();
        std::this_thread::yield();
    }

    STAR_CHECK(first->getState() == AssetState::Ready);
    STAR_CHECK(second->getState() == AssetState::Ready);
    STAR_CHECK(asText(first->getBytes()) == "derived data");
    STAR_CHECK(first->getBytes().data() == second->getBytes().data());
}

STAR_TEST(asset_manager_retains_then_reloads_unreferenced_blobs) {
    AssetFile file;
    file.write("original");
    const auto guid = Guid::generate();
    AssetManager manager(2);
    STAR_CHECK(manager.registerBlob(guid, file.path).has_value());

    auto handle = manager.load(guid).value();
    STAR_CHECK(asText(handle.getBytes()) == "original");
    file.write("replacement");
    manager.processCompletedLoads();
    manager.processCompletedLoads();
    STAR_CHECK(asText(manager.load(guid)->getBytes()) == "original");

    handle = {};
    manager.processCompletedLoads();
    STAR_CHECK(asText(manager.load(guid)->getBytes()) == "original");
    manager.processCompletedLoads();
    manager.processCompletedLoads();
    STAR_CHECK(asText(manager.load(guid)->getBytes()) == "replacement");
}

STAR_TEST(asset_handles_keep_results_after_manager_shutdown) {
    AssetFile file;
    file.write("");
    const auto guid = Guid::generate();
    const auto missing = Guid::generate();
    AssetBlobHandle handle;
    AssetBlobHandle failed;
    {
        AssetFile missing_file;
        AssetManager manager;
        STAR_CHECK(manager.loadAsync(missing).error() == AssetLoadError::NotRegistered);
        STAR_CHECK(manager.registerBlob(guid, file.path).has_value());
        STAR_CHECK(manager.registerBlob(missing, missing_file.path).has_value());
        handle = manager.loadAsync(guid).value();
        failed = manager.loadAsync(missing).value();
    }
    STAR_CHECK(handle.getState() == AssetState::Ready);
    STAR_CHECK(handle.getBytes().empty());
    STAR_CHECK(failed.getState() == AssetState::Failed);
    STAR_CHECK(failed.getError() == AssetLoadError::ReadFailed);
}

STAR_TEST(asset_manager_loads_a_derived_texture_without_an_importer) {
    const TextureAsset texture{TextureColorSpace::Color,
                               {{2, 1, {1, 2, 3, 255, 4, 5, 6, 255}}, {1, 1, {2, 3, 4, 255}}}};
    const auto payload = writeTextureAsset(texture);
    STAR_CHECK(payload.has_value());
    AssetFile file;
    file.write(asText(*payload));
    AssetManager manager;
    const auto guid = Guid::generate();
    STAR_CHECK(manager.registerBlob(guid, file.path).has_value());
    const auto loaded = manager.load(guid);
    STAR_CHECK(loaded.has_value());
    STAR_CHECK(readTextureAsset(loaded->getBytes()).value() == texture);

    const auto bytes = std::span(*payload);
    const auto truncated = readTextureAsset(bytes.first(bytes.size() - 1));
    STAR_CHECK(!truncated.has_value());
    STAR_CHECK(truncated.error() == TextureAssetError::InvalidMipChain);
    auto invalid = texture;
    invalid.mips.at(1).width = 2;
    STAR_CHECK(!writeTextureAsset(invalid).has_value());
}

STAR_TEST(asset_manager_loads_a_static_model_without_model_parsers) {
    StaticModelAsset model;
    auto &part = model.parts.emplace_back();
    part.name = "Triangle";
    part.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
    part.indices = {0, 1, 2};
    part.material.emplace();
    part.material->blend = MaterialBlendMode::Masked;
    part.material->alpha_cutoff = 0.25F;
    const auto payload = writeModelAsset(model);
    STAR_CHECK(payload.has_value());

    AssetFile file;
    file.write(asText(*payload));
    AssetManager manager;
    const auto guid = Guid::generate();
    STAR_CHECK(manager.registerBlob(guid, file.path).has_value());
    const auto loaded = manager.load(guid);
    STAR_CHECK(loaded.has_value());
    const auto decoded = readModelAsset(loaded->getBytes());
    STAR_CHECK(decoded.has_value());
    STAR_CHECK(decoded->parts.at(0).name == "Triangle");
    STAR_CHECK(decoded->parts.at(0).indices == part.indices);
    STAR_CHECK(decoded->parts.at(0).vertices.at(1).position == part.vertices.at(1).position);
    STAR_CHECK(decoded->parts.at(0).material->blend == MaterialBlendMode::Masked);
    STAR_CHECK(decoded->parts.at(0).material->alpha_cutoff == 0.25F);

    auto corrupt = *payload;
    corrupt.at(corrupt.size() - 1) = std::byte{1};
    STAR_CHECK(!readModelAsset(corrupt).has_value());
}

STAR_TEST(typed_asset_requests_share_background_decode_and_retain_the_result) {
    const TextureAsset texture{TextureColorSpace::Normal, {{1, 1, {128, 128, 255, 255}}}};
    AssetFile file;
    file.write(asText(writeTextureAsset(texture).value()));
    const AssetRef<TextureAsset> reference{Guid::generate()};
    AssetHandle<TextureAsset> retained;
    {
        AssetManager manager;
        STAR_CHECK(manager.registerAsset<TextureAsset>(reference.guid, file.path).has_value());
        const auto wrong_type = manager.loadAsync<StaticModelAsset>(reference.guid);
        STAR_CHECK(!wrong_type.has_value());
        STAR_CHECK(wrong_type.error() == AssetLoadError::TypeMismatch);
        auto first = reference.loadAsync(manager);
        STAR_CHECK(first.has_value());
        STAR_CHECK(first->getState() == AssetState::Loading);
        STAR_CHECK(first->get() == nullptr);
        auto second = reference.load(manager);
        STAR_CHECK(second.has_value());
        STAR_CHECK(first->isReady());
        STAR_CHECK(second->get() == first->get());
        retained = std::move(*second);
    }
    STAR_CHECK(retained.isReady());
    STAR_CHECK(*retained.get() == texture);
}

STAR_TEST(typed_asset_requests_report_decode_failures) {
    AssetFile file;
    file.write("invalid texture");
    const auto id = Guid::generate();
    AssetHandle<TextureAsset> handle;
    {
        AssetManager manager;
        STAR_CHECK(manager.registerAsset<TextureAsset>(id, file.path).has_value());
        handle = manager.loadAsync<TextureAsset>(id).value();
        const auto failed = manager.load<TextureAsset>(id);
        STAR_CHECK(!failed.has_value());
        STAR_CHECK(failed.error() == AssetLoadError::DecodeFailed);
    }
    STAR_CHECK(handle.getState() == AssetState::Failed);
    STAR_CHECK(handle.getError() == AssetLoadError::DecodeFailed);
    STAR_CHECK(handle.get() == nullptr);
}

STAR_TEST(material_assets_decode_and_resolve_parameters) {
    const auto shader = Guid::generate();
    const auto text = "Type: star::Material\n"
                      "Version: 1\n"
                      "Properties:\n"
                      "  Shader: " +
                      shader.toString() +
                      "\n"
                      "  Parameters:\n"
                      "    BaseColor: [0.1, 0.2, 0.3, 0.5]\n"
                      "    Metallic: 0.9\n"
                      "    Roughness: 0.25\n"
                      "    Custom: 4\n"
                      "  RenderState:\n"
                      "    Blend: Masked\n"
                      "    Shading: Lit\n"
                      "    DoubleSided: true\n"
                      "    DepthTest: true\n";
    AssetFile file;
    file.write(text);

    const auto guid = Guid::generate();
    AssetManager manager;
    STAR_CHECK(manager.registerAsset<MaterialAsset>(guid, file.path).has_value());
    const auto handle = manager.load<MaterialAsset>(guid);
    STAR_CHECK(handle.has_value());
    STAR_CHECK(handle->get() != nullptr);
    STAR_CHECK(handle->get()->shader == shader);
    STAR_CHECK(handle->get()->state.blend == MaterialBlendMode::Masked);

    const auto resolved = resolveMaterial(*handle->get());
    STAR_CHECK(resolved.has_value());
    STAR_CHECK((resolved->base_color == std::array<float, 4>{0.1F, 0.2F, 0.3F, 0.5F}));
    STAR_CHECK(resolved->metallic == 0.9F);
    STAR_CHECK(resolved->roughness == 0.25F);
    STAR_CHECK(resolved->state.blend == MaterialBlendMode::Masked);
}

STAR_TEST(resolve_material_rejects_malformed_known_parameters) {
    MaterialAsset material;
    material.shader = Guid::generate();
    material.parameters["BaseColor"] = serialization::ArchiveValue{serialization::ArchiveValue::Sequence{
        serialization::ArchiveValue{serialization::PlainScalar{"1"}},
        serialization::ArchiveValue{serialization::PlainScalar{"0"}},
        serialization::ArchiveValue{serialization::PlainScalar{"0"}},
    }};
    const auto short_color = resolveMaterial(material);
    STAR_CHECK(!short_color && short_color.error().code == serialization::ArchiveErrorCode::InvalidInput);

    material.parameters.erase("BaseColor");
    material.parameters["Roughness"] = serialization::ArchiveValue{serialization::PlainScalar{"2.0"}};
    const auto out_of_range = resolveMaterial(material);
    STAR_CHECK(!out_of_range && out_of_range.error().code == serialization::ArchiveErrorCode::InvalidInput);
}

STAR_TEST(texture_asset_round_trips_block_compressed_mips) {
    const std::vector<std::uint8_t> block(8, 0x7F);
    TextureAsset texture{TextureColorSpace::Color, {{4, 4, block}}, TextureFormat::Bc1};
    const auto payload = writeTextureAsset(texture);
    STAR_CHECK(payload.has_value());
    const auto decoded = readTextureAsset(std::span<const std::byte>(*payload));
    STAR_CHECK(decoded.has_value());
    STAR_CHECK(decoded->format == TextureFormat::Bc1);
    STAR_CHECK(*decoded == texture);

    auto truncated = texture;
    truncated.mips.front().data.resize(4);
    STAR_CHECK(!writeTextureAsset(truncated).has_value());

    auto empty = texture;
    empty.mips.front().data.clear();
    STAR_CHECK(!writeTextureAsset(empty).has_value());
}

STAR_TEST(model_asset_round_trips_material_state) {
    StaticModelAsset model;
    auto &part = model.parts.emplace_back();
    part.vertices = {{{0, 0, 0}, {0, 0, 1}, {}}, {{1, 0, 0}, {0, 0, 1}, {}}, {{0, 1, 0}, {0, 0, 1}, {}}};
    part.indices = {0, 1, 2};
    auto &material = part.material.emplace();
    material.blend = MaterialBlendMode::Transparent;
    material.shading = MaterialShadingModel::Unlit;
    material.double_sided = true;
    const auto payload = writeModelAsset(model);
    STAR_CHECK(payload.has_value());
    const auto decoded = readModelAsset(std::span<const std::byte>(*payload));
    STAR_CHECK(decoded.has_value());
    STAR_CHECK(decoded->parts.at(0).material->blend == MaterialBlendMode::Transparent);
    STAR_CHECK(decoded->parts.at(0).material->shading == MaterialShadingModel::Unlit);
    STAR_CHECK(decoded->parts.at(0).material->double_sided);
}
