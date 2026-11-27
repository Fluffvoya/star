#include "asset-pipeline/asset_database.hpp"

#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace star::asset_pipeline {
namespace {

using serialization::ArchiveValue;
using serialization::PlainScalar;
using Mapping = ArchiveValue::Mapping;
using ErrorCode = AssetDatabaseErrorCode;

const ArchiveValue *findField(const Mapping &mapping, std::string_view key) {
    const auto it = mapping.find(key);
    return it == mapping.end() ? nullptr : &it->second;
}

template <typename T> const T *getField(const Mapping &mapping, std::string_view key) {
    const auto *field = findField(mapping, key);
    return field ? std::get_if<T>(&field->value) : nullptr;
}

std::string_view getText(const Mapping &mapping, std::string_view key) {
    if (const auto *quoted = getField<std::string>(mapping, key)) {
        return *quoted;
    }
    if (const auto *plain = getField<PlainScalar>(mapping, key)) {
        return plain->text;
    }
    return {};
}

Guid getGuid(const Mapping &mapping) { return Guid::parse(getText(mapping, "Guid")).value_or(Guid{}); }

std::unexpected<AssetDatabaseError> invalidMeta(std::string field) {
    return std::unexpected(AssetDatabaseError{ErrorCode::InvalidMeta, {}, std::move(field)});
}

std::expected<void, AssetDatabaseError> validateMeta(const AssetMeta &meta) {
    if (!meta.guid.isValid()) {
        return invalidMeta("Guid");
    }
    if (meta.importer.empty()) {
        return invalidMeta("Importer");
    }
    if (meta.importer_version == 0) {
        return invalidMeta("ImporterVersion");
    }
    std::set<Guid> guids{meta.guid};
    for (std::size_t index = 0; index < meta.sub_assets.size(); ++index) {
        const SubAsset &sub_asset = meta.sub_assets[index];
        const std::string prefix = "SubAssets[" + std::to_string(index) + "].";
        if (!sub_asset.guid.isValid()) {
            return invalidMeta(prefix + "Guid");
        }
        if (!guids.insert(sub_asset.guid).second) {
            return std::unexpected(AssetDatabaseError{ErrorCode::DuplicateGuid, {}, prefix + "Guid"});
        }
        if (sub_asset.name.empty()) {
            return invalidMeta(prefix + "Name");
        }
        if (sub_asset.type.empty()) {
            return invalidMeta(prefix + "Type");
        }
    }
    return {};
}

} // namespace

std::expected<AssetMeta, AssetDatabaseError> readAssetMeta(std::string_view yaml) {
    auto root = serialization::readYamlValue(yaml);
    if (!root) {
        return invalidMeta(root.error().path);
    }
    auto *mapping = std::get_if<Mapping>(&root->value);
    if (!mapping || getText(*mapping, "StarFile") != "Meta") {
        return invalidMeta("StarFile");
    }

    AssetMeta meta;
    meta.guid = getGuid(*mapping);
    meta.importer = getText(*mapping, "Importer");
    const auto version = getText(*mapping, "ImporterVersion");
    if (version.empty()) {
        return invalidMeta("ImporterVersion");
    }
    const auto version_end = version.data() + version.size();
    const auto parsed = std::from_chars(version.data(), version_end, meta.importer_version);
    if (parsed.ec != std::errc{} || parsed.ptr != version_end) {
        return invalidMeta("ImporterVersion");
    }

    if (findField(*mapping, "Settings")) {
        const auto *settings = getField<Mapping>(*mapping, "Settings");
        if (!settings) {
            return invalidMeta("Settings");
        }
        meta.settings = *settings;
    }
    if (findField(*mapping, "SubAssets")) {
        const auto *sub_assets = getField<ArchiveValue::Sequence>(*mapping, "SubAssets");
        if (!sub_assets) {
            return invalidMeta("SubAssets");
        }
        for (const auto &value : *sub_assets) {
            const auto *sub = std::get_if<Mapping>(&value.value);
            if (!sub) {
                return invalidMeta("SubAssets[" + std::to_string(meta.sub_assets.size()) + "]");
            }
            SubAsset entry{std::string(getText(*sub, "Name")), std::string(getText(*sub, "Type")), getGuid(*sub), *sub};
            for (const char *key : {"Name", "Type", "Guid"}) {
                entry.extensions.erase(key);
            }
            meta.sub_assets.push_back(std::move(entry));
        }
    }
    if (auto valid = validateMeta(meta); !valid) {
        return std::unexpected(valid.error());
    }
    for (const char *key : {"StarFile", "Guid", "Importer", "ImporterVersion", "Settings", "SubAssets"}) {
        mapping->erase(key);
    }
    meta.extensions = std::move(*mapping);
    return meta;
}

std::expected<std::string, AssetDatabaseError> writeAssetMeta(const AssetMeta &meta) {
    if (auto valid = validateMeta(meta); !valid) {
        return std::unexpected(valid.error());
    }

    ArchiveValue::Sequence sub_assets;
    sub_assets.reserve(meta.sub_assets.size());
    for (const SubAsset &sub : meta.sub_assets) {
        Mapping entry = sub.extensions;
        entry["Name"] = ArchiveValue{sub.name};
        entry["Type"] = ArchiveValue{sub.type};
        entry["Guid"] = ArchiveValue{sub.guid.toString()};
        sub_assets.push_back(ArchiveValue{std::move(entry)});
    }

    Mapping fields = meta.extensions;
    fields["StarFile"] = ArchiveValue{std::string("Meta")};
    fields["Guid"] = ArchiveValue{meta.guid.toString()};
    fields["Importer"] = ArchiveValue{meta.importer};
    fields["ImporterVersion"] = ArchiveValue{PlainScalar{std::to_string(meta.importer_version)}};
    fields["Settings"] = ArchiveValue{meta.settings};
    fields["SubAssets"] = ArchiveValue{std::move(sub_assets)};
    auto yaml = serialization::writeYamlValue(ArchiveValue{std::move(fields)});
    if (!yaml) {
        return invalidMeta(yaml.error().path);
    }
    return std::move(*yaml);
}

std::expected<void, AssetDatabaseError> AssetDatabase::scan(const std::filesystem::path &content_root) {
    AssetDatabase next;
    std::error_code io;
    std::filesystem::recursive_directory_iterator entry(content_root, io);
    if (io) {
        return std::unexpected(AssetDatabaseError{ErrorCode::IoFailure, content_root, {}});
    }

    for (const std::filesystem::recursive_directory_iterator end; entry != end;) {
        const auto sidecar = entry->path();
        if (sidecar.extension() == ".meta" && std::filesystem::is_regular_file(entry->symlink_status(io))) {
            auto source = sidecar;
            source.replace_extension();
            const auto status = std::filesystem::symlink_status(source, io);
            if (!std::filesystem::is_regular_file(status)) {
                const auto code =
                    io && io != std::errc::no_such_file_or_directory ? ErrorCode::IoFailure : ErrorCode::MissingSource;
                return std::unexpected(AssetDatabaseError{code, source, {}});
            }
            std::ifstream input(sidecar, std::ios::binary);
            std::ostringstream bytes;
            if (!input.is_open() || (input.peek() != std::char_traits<char>::eof() && !(bytes << input.rdbuf())) ||
                input.bad()) {
                return std::unexpected(AssetDatabaseError{ErrorCode::IoFailure, sidecar, {}});
            }
            auto meta = readAssetMeta(std::move(bytes).str());
            if (!meta) {
                meta.error().path = sidecar;
                return std::unexpected(meta.error());
            }

            const auto relative = source.lexically_relative(content_root);
            if (!next.m_locations.emplace(meta->guid, AssetLocation{relative, std::nullopt}).second) {
                return std::unexpected(AssetDatabaseError{ErrorCode::DuplicateGuid, sidecar, "Guid"});
            }
            for (std::size_t index = 0; index < meta->sub_assets.size(); ++index) {
                if (!next.m_locations.emplace(meta->sub_assets[index].guid, AssetLocation{relative, index}).second) {
                    return std::unexpected(AssetDatabaseError{ErrorCode::DuplicateGuid, sidecar,
                                                              "SubAssets[" + std::to_string(index) + "].Guid"});
                }
            }
            next.m_metadata.emplace(relative, std::move(*meta));
        }
        if (!io) {
            entry.increment(io);
        }
        if (io) {
            return std::unexpected(AssetDatabaseError{ErrorCode::IoFailure, sidecar, {}});
        }
    }

    *this = std::move(next);
    return {};
}

const AssetMeta *AssetDatabase::findMeta(const std::filesystem::path &source) const {
    const auto it = m_metadata.find(source.lexically_normal());
    return it == m_metadata.end() ? nullptr : &it->second;
}

const AssetLocation *AssetDatabase::findAsset(Guid guid) const {
    const auto it = m_locations.find(guid);
    return it == m_locations.end() ? nullptr : &it->second;
}

} // namespace star::asset_pipeline
