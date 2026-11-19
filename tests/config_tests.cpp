#include "test_support.hpp"

#include "core/config.hpp"
#include "core/guid.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using namespace std::string_view_literals;

class ConfigTestFiles final {
  public:
    ConfigTestFiles()
        : m_root(std::filesystem::temp_directory_path() / ("star-config-" + star::Guid::generate().toString())) {
        STAR_CHECK(std::filesystem::create_directory(m_root));
    }

    ~ConfigTestFiles() {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    std::filesystem::path write(std::string_view name, std::string_view content) const {
        const auto path = m_root / name;
        std::ofstream file(path, std::ios::binary);
        file << content;
        file.close();
        STAR_CHECK(file.good());
        return path;
    }

  private:
    std::filesystem::path m_root;
};

STAR_TEST(configLoadsLayersInOrderAndPreservesOtherKeys) {
    const ConfigTestFiles files;
    const star::ConfigFiles layers{
        .engine = files.write("engine.yaml", "---\nvideo: {width: 640, height: 480}\n"),
        .project = files.write("project.yaml", "video: {width: 800, preset: high}\n"),
        .platform = files.write("platform.yaml", "video.width: 1024\nplatform: linux\n"),
        .user = files.write("user.yaml", "video:\n  width: 1280\n  label: \"custom = value\"\n"),
    };

    const auto from_files = star::Config::load(layers);
    STAR_CHECK(from_files);
    STAR_CHECK(from_files->get("video.width") == "1280");
    STAR_CHECK(from_files->get("video.height") == "480");
    STAR_CHECK(from_files->get("video.preset") == "high");
    STAR_CHECK(from_files->get("platform") == "linux");
    STAR_CHECK(from_files->get("video.label") == "custom = value");

    const auto config = star::Config::load(
        layers, std::array{"--other"sv, "--set=video.width=1440"sv, "--set=video.width=1600"sv, "--set=extra=a=b"sv});
    STAR_CHECK(config && config->get("video.width") == "1600" && config->get("extra") == "a=b");
    STAR_CHECK(!config->get("missing"));
}

STAR_TEST(configReportsInvalidFilesAndKeys) {
    const ConfigTestFiles files;
    star::ConfigFiles layers;
    layers.platform = files.write("invalid.yaml", "setting: [a, b\n");
    const auto malformed = star::Config::load(layers);
    STAR_CHECK(!malformed && malformed.error().code == star::ConfigError::InvalidYaml);
    STAR_CHECK(malformed.error().layer == star::ConfigLayer::Platform && malformed.error().line > 0);

    layers.platform = files.write("duplicate.yaml", "video: {width: 640}\nvideo.width: 800\n");
    const auto duplicate = star::Config::load(layers);
    STAR_CHECK(!duplicate && duplicate.error().code == star::ConfigError::DuplicateKey);

    layers.platform = files.write("sequence.yaml", "setting: [a, b]\n");
    const auto sequence = star::Config::load(layers);
    STAR_CHECK(!sequence && sequence.error().code == star::ConfigError::UnsupportedValue);

    layers.platform = layers.platform.parent_path() / "missing.yaml";
    const auto missing_file = star::Config::load(layers);
    STAR_CHECK(!missing_file && missing_file.error().code == star::ConfigError::FileRead);

    const auto invalid_argument = star::Config::load({}, std::array{"--set=wrong"sv});
    STAR_CHECK(!invalid_argument && invalid_argument.error().code == star::ConfigError::InvalidOverride);
    STAR_CHECK(invalid_argument.error().layer == star::ConfigLayer::CommandLine);
}

STAR_TEST(configAppliesCVarsAtomicallyWithCommandLinePriority) {
    const ConfigTestFiles files;
    star::ConfigFiles layers;
    layers.engine = files.write("cvars.yaml", "CVars:\n  config.Level: 3\n  config.Visible: false\n");
    star::CVar<int> level{"config.Level", 2, "Quality level"};
    star::CVar<bool> visible{"config.Visible", true, "Visible"};

    const auto bad = star::Config::load(layers, std::array{"--set=CVars.config.Visible=maybe"sv});
    STAR_CHECK(bad);
    const auto rejected = bad->applyCVars();
    STAR_CHECK(!rejected && rejected.error() == star::CVarError::InvalidValue);
    STAR_CHECK(level.get() == 2 && visible.get() == true);

    const auto unknown = star::Config::load(layers, std::array{"--set=CVars.config.Unknown=3"sv});
    STAR_CHECK(unknown);
    const auto unregistered = unknown->applyCVars();
    STAR_CHECK(!unregistered && unregistered.error() == star::CVarError::NotFound);
    STAR_CHECK(level.get() == 2 && visible.get() == true);

    const auto scalar = star::Config::load(layers, std::array{"--set=CVars=true"sv});
    STAR_CHECK(scalar && !scalar->applyCVars());
    STAR_CHECK(level.get() == 2 && visible.get() == true);

    const auto config = star::Config::load(layers, std::array{"--set=CVars.config.Level=4"sv});
    STAR_CHECK(config && config->applyCVars());
    STAR_CHECK(level.get() == 4 && visible.get() == false);
}

} // namespace
