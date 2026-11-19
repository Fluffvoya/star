#include "core/config.hpp"

#include <ryml.hpp>

#include <array>
#include <exception>
#include <fstream>
#include <new>
#include <set>
#include <utility>
#include <vector>

namespace star {

namespace {

using ConfigValues = std::map<std::string, std::string, std::less<>>;

constexpr std::string_view cvar_prefix = "CVars.";
constexpr std::string_view override_prefix = "--set=";

bool isConfigKey(std::string_view key) {
    return !key.empty() && !key.starts_with('.') && !key.ends_with('.') && key.find("..") == std::string_view::npos &&
           key.find_first_of("=\t\r\n ") == std::string_view::npos && key.find('\0') == std::string_view::npos;
}

bool isPlainNode(ryml::ConstNodeRef node) {
    return !node.has_anchor() && !node.is_ref() && !node.has_key_tag() && !node.has_val_tag();
}

std::expected<void, ConfigError> flattenMapping(ryml::ConstNodeRef root, ConfigValues &values) {
    if (!root.is_map() || !isPlainNode(root)) {
        return std::unexpected(ConfigError::UnsupportedValue);
    }

    std::set<std::string> keys;
    std::vector<std::pair<ryml::ConstNodeRef, std::string>> pending;
    pending.emplace_back(root, std::string{});
    while (!pending.empty()) {
        auto [parent, prefix] = std::move(pending.back());
        pending.pop_back();
        for (const auto child : parent.children()) {
            if (!child.has_key() || !isPlainNode(child) || child.key_is_null()) {
                return std::unexpected(ConfigError::UnsupportedValue);
            }
            const auto key = child.key();
            const std::string_view name(key.str, key.len);
            if (!isConfigKey(name)) {
                return std::unexpected(ConfigError::UnsupportedValue);
            }

            std::string path = prefix;
            path.append(name);
            if (!keys.insert(path).second) {
                return std::unexpected(ConfigError::DuplicateKey);
            }
            if (child.is_map()) {
                path.push_back('.');
                pending.emplace_back(child, std::move(path));
            } else if (child.has_val() && !child.val_is_null()) {
                const auto value = child.val();
                values.insert_or_assign(std::move(path), std::string(value.str, value.len));
            } else {
                return std::unexpected(ConfigError::UnsupportedValue);
            }
        }
    }
    return {};
}

std::expected<void, ConfigLoadError> loadYaml(const std::filesystem::path &path, ConfigLayer layer,
                                              ConfigValues &values) {
    if (path.empty()) {
        return {};
    }
    try {
        std::ifstream file(path, std::ios::binary);
        std::string source;
        std::array<char, 4096> buffer;
        while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() != 0) {
            source.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
        }
        if (!file.eof() || file.bad()) {
            return std::unexpected(ConfigLoadError{ConfigError::FileRead, layer});
        }

        ryml::Callbacks callbacks;
        callbacks.set_user_data(&layer).set_error_parse(
            [](ryml::csubstr, const ryml::ErrorDataParse &error, void *data) {
                const auto &position = error.ymlloc;
                const auto line = position.line == ryml::npos ? 0 : position.line + 1;
                const auto column = position.col == ryml::npos ? 0 : position.col + 1;
                throw ConfigLoadError{ConfigError::InvalidYaml, *static_cast<ConfigLayer *>(data), line, column};
            });
        ryml::EventHandlerTree handler(callbacks);
        ryml::Parser parser(&handler);
        const ryml::Tree tree = ryml::parse_in_place(&parser, ryml::substr(source.data(), source.size()));
        if (tree.empty() || tree.rootref().type() == ryml::NOTYPE) {
            return {};
        }
        auto root = tree.rootref();
        if (root.is_stream()) {
            if (root.num_children() != 1) {
                return std::unexpected(ConfigLoadError{ConfigError::UnsupportedValue, layer});
            }
            root = root.first_child();
        }
        if (const auto parsed = flattenMapping(root, values); !parsed) {
            return std::unexpected(ConfigLoadError{parsed.error(), layer});
        }
        return {};
    } catch (const ConfigLoadError &error) {
        return std::unexpected(error);
    } catch (const std::bad_alloc &) {
        return std::unexpected(ConfigLoadError{ConfigError::AllocationFailed, layer});
    } catch (const std::exception &) {
        return std::unexpected(ConfigLoadError{ConfigError::InvalidYaml, layer});
    }
}

} // namespace

std::expected<Config, ConfigLoadError> Config::load(const ConfigFiles &files,
                                                    std::span<const std::string_view> arguments) noexcept {
    auto allocation_layer = ConfigLayer::Engine;
    try {
        Config result;
        const std::array layers{
            std::pair{ConfigLayer::Engine, &files.engine}, std::pair{ConfigLayer::Project, &files.project},
            std::pair{ConfigLayer::Platform, &files.platform}, std::pair{ConfigLayer::User, &files.user}};
        for (const auto &[layer, path] : layers) {
            if (const auto loaded = loadYaml(*path, layer, result.m_values); !loaded) {
                return std::unexpected(loaded.error());
            }
        }

        allocation_layer = ConfigLayer::CommandLine;
        for (std::string_view argument : arguments) {
            if (argument == "--set") {
                return std::unexpected(ConfigLoadError{ConfigError::InvalidOverride, ConfigLayer::CommandLine});
            }
            if (!argument.starts_with(override_prefix)) {
                continue;
            }
            argument.remove_prefix(override_prefix.size());
            const std::size_t separator = argument.find('=');
            const auto key = argument.substr(0, separator);
            if (separator == std::string_view::npos || !isConfigKey(key)) {
                return std::unexpected(ConfigLoadError{ConfigError::InvalidOverride, ConfigLayer::CommandLine});
            }
            result.m_values.insert_or_assign(std::string(key), std::string(argument.substr(separator + 1)));
        }
        return result;
    } catch (const std::exception &) {
        return std::unexpected(ConfigLoadError{ConfigError::AllocationFailed, allocation_layer});
    }
}

std::optional<std::string_view> Config::get(std::string_view dotted_key) const noexcept {
    const auto found = m_values.find(dotted_key);
    if (found == m_values.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::expected<void, CVarError> Config::applyCVars() const noexcept {
    if (m_values.contains("CVars")) {
        return std::unexpected(CVarError::InvalidValue);
    }
    try {
        std::vector<CVarAssignment> assignments;
        for (auto found = m_values.lower_bound(cvar_prefix);
             found != m_values.end() && found->first.starts_with(cvar_prefix); ++found) {
            const auto &[key, value] = *found;
            assignments.push_back({std::string_view(key).substr(cvar_prefix.size()), value});
        }
        return CVarRegistry::setFromStrings(assignments);
    } catch (const std::exception &) {
        return std::unexpected(CVarError::AllocationFailed);
    }
}

} // namespace star
