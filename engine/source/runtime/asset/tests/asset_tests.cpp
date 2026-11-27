#include "asset/asset_manager.hpp"
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
