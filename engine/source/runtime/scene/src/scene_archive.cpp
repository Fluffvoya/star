#include "scene_archive_access.hpp"

#include "scene/world.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <set>

namespace star {
namespace {

using namespace serialization;
using Mapping = ArchiveValue::Mapping;
using Sequence = ArchiveValue::Sequence;

template <class T> const T *field(const Mapping &mapping, std::string_view key) {
    const auto found = mapping.find(key);
    return found == mapping.end() ? nullptr : std::get_if<T>(&found->second.value);
}

std::optional<std::uint64_t> number(const Mapping &mapping, std::string_view key) {
    const auto *scalar = field<PlainScalar>(mapping, key);
    if (scalar == nullptr) {
        return {};
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(scalar->text.data(), scalar->text.data() + scalar->text.size(), value);
    if (error != std::errc{} || end != scalar->text.data() + scalar->text.size()) {
        return {};
    }
    return value;
}

std::optional<Guid> guid(const Mapping &mapping, std::string_view key) {
    const auto *text = field<std::string>(mapping, key);
    const auto *scalar = field<PlainScalar>(mapping, key);
    if (text == nullptr && scalar == nullptr) {
        return {};
    }
    const auto value = Guid::parse(text != nullptr ? *text : scalar->text);
    return value && value->isValid() ? std::optional(*value) : std::nullopt;
}

auto invalid(std::string path) {
    return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::move(path)});
}
auto failed() { return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, "/GameObjects"}); }

struct SceneDocument {
    Mapping fields;
    std::vector<ArchivedObject> objects;
};

void normalizeVector(Mapping &fields, std::string_view name, std::string_view axes) {
    const auto found = fields.find(name);
    if (found == fields.end()) {
        return;
    }
    auto *sequence = std::get_if<Sequence>(&found->second.value);
    if (sequence == nullptr || sequence->size() != axes.size()) {
        return;
    }
    Mapping map;
    for (std::size_t i = 0; i < axes.size(); ++i) {
        map.emplace(std::string(1, axes[i]), std::move((*sequence)[i]));
    }
    found->second = ArchiveValue{std::move(map)};
}

std::expected<SceneDocument, ArchiveError> decodeDocument(ArchiveValue source, std::string_view kind) {
    auto *root = std::get_if<Mapping>(&source.value);
    if (root == nullptr) {
        return invalid("/");
    }
    const auto *file = field<std::string>(*root, "StarFile");
    if (file == nullptr || *file != kind) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::TypeMismatch, "/StarFile"});
    }
    if (number(*root, "Version") != 1) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedVersion, "/Version"});
    }
    auto entry = root->extract("GameObjects");
    auto *sequence = entry.empty() ? nullptr : std::get_if<Sequence>(&entry.mapped().value);
    if (sequence == nullptr) {
        return invalid("/GameObjects");
    }
    SceneDocument result{std::move(*root), {}};
    result.fields.erase("StarFile");
    result.fields.erase("Version");

    std::set<Guid> identities;
    for (auto &value : *sequence) {
        auto *object = std::get_if<Mapping>(&value.value);
        if (object == nullptr) {
            return invalid("/GameObjects");
        }
        const auto identity = guid(*object, "Guid");
        auto *name = field<std::string>(*object, "Name");
        if (!identity || !identities.insert(*identity).second || name == nullptr) {
            return invalid("/GameObjects");
        }
        const auto path = "/GameObjects/" + identity->toString();
        const auto order = number(*object, "Order");
        if (object->contains("Order") && !order) {
            return invalid(path + "/Order");
        }
        if (kind == "Prefab" && object->contains("PrefabInstance")) {
            return invalid(path + "/PrefabInstance");
        }
        bool active = true;
        if (object->contains("Active")) {
            const auto *flag = field<PlainScalar>(*object, "Active");
            if (flag == nullptr || (flag->text != "true" && flag->text != "false")) {
                return invalid(path + "/Active");
            }
            active = flag->text == "true";
        }
        ArchivedObject record{*identity, {}, *name, order.value_or(result.objects.size()), 0, active, {}, {}, {}};
        if (const auto components = object->extract("Components"); !components.empty()) {
            const auto *items = std::get_if<Sequence>(&components.mapped().value);
            if (items == nullptr) {
                return invalid(path + "/Components");
            }
            std::set<std::string> seen;
            for (const auto &component : *items) {
                const auto *mapping = std::get_if<Mapping>(&component.value);
                if (mapping == nullptr) {
                    return invalid(path + "/Components");
                }
                const auto *type = field<std::string>(*mapping, "Type");
                const auto version = number(*mapping, "Version").value_or(1);
                if (type == nullptr || type->empty() || !seen.insert(*type).second || version == 0 ||
                    version > UINT32_MAX || (mapping->contains("Version") && !number(*mapping, "Version"))) {
                    return invalid(path + "/Components");
                }
                if (*type == "star::Hierarchy") {
                    if (const auto found = mapping->find("parent");
                        found != mapping->end() && !std::holds_alternative<std::monostate>(found->second.value)) {
                        const auto parent = guid(*mapping, "parent");
                        if (!parent) {
                            return invalid(path + "/Hierarchy/parent");
                        }
                        record.parent = *parent;
                    }
                    const auto sibling = number(*mapping, "sibling_index");
                    if (mapping->contains("sibling_index") && (!sibling || *sibling > UINT32_MAX)) {
                        return invalid(path + "/Hierarchy/sibling_index");
                    }
                    record.sibling =
                        static_cast<std::uint32_t>(sibling.value_or(record.order <= UINT32_MAX ? record.order : 0));
                    auto extensions = *mapping;
                    for (const auto key : {"Type", "Version", "parent", "sibling_index"}) {
                        extensions.erase(key);
                    }
                    if (!extensions.empty() || version != 1) {
                        extensions.emplace("Version", ArchiveValue{PlainScalar{std::to_string(version)}});
                        record.hierarchy_fields = std::move(extensions);
                    }
                    continue;
                }
                auto fields = *mapping;
                fields.erase("Type");
                fields.erase("Version");
                if (*type == "star::Transform") {
                    normalizeVector(fields, "position", "xyz");
                    normalizeVector(fields, "rotation", "xyzw");
                    normalizeVector(fields, "scale", "xyz");
                }
                record.components.push_back({*type, static_cast<std::uint32_t>(version), std::move(fields)});
            }
        }
        for (const auto key : {"Guid", "Name", "Order", "Active"}) {
            object->erase(key);
        }
        record.fields.merge(*object);
        result.objects.push_back(std::move(record));
    }
    std::size_t roots = 0;
    for (const auto &object : result.objects) {
        if (object.parent.isValid() && !identities.contains(object.parent)) {
            return invalid("/GameObjects/" + object.guid.toString() + "/Hierarchy/parent");
        }
        roots += !object.parent.isValid();
    }
    if (kind == "Prefab" && roots != 1) {
        return invalid("/GameObjects");
    }
    std::ranges::stable_sort(result.objects, {}, &ArchivedObject::order);
    return result;
}

std::expected<SceneDocument, ArchiveError> readDocument(std::string_view source, std::string_view kind) {
    return readYamlValue(source).and_then(
        [kind](ArchiveValue value) { return decodeDocument(std::move(value), kind); });
}

Mapping encodeDocument(SceneDocument document, std::string_view kind) {
    std::ranges::sort(document.objects, {}, &ArchivedObject::order);
    for (std::size_t index = 0; index < document.objects.size(); ++index) {
        document.objects[index].order = index;
    }
    std::ranges::sort(document.objects, {}, &ArchivedObject::guid);
    Sequence objects;
    for (auto &object : document.objects) {
        auto fields = std::move(object.fields);
        fields.insert_or_assign("Guid", ArchiveValue{object.guid.toString()});
        fields.insert_or_assign("Name", ArchiveValue{std::move(object.name)});
        fields.insert_or_assign("Order", ArchiveValue{PlainScalar{std::to_string(object.order)}});
        if (!object.active) {
            fields.insert_or_assign("Active", ArchiveValue{PlainScalar{"false"}});
        }
        Sequence components;
        for (auto &component : object.components) {
            component.fields.insert_or_assign("Type", ArchiveValue{std::move(component.type_name)});
            if (component.version != 1) {
                component.fields.insert_or_assign("Version",
                                                  ArchiveValue{PlainScalar{std::to_string(component.version)}});
            }
            components.push_back(ArchiveValue{std::move(component.fields)});
        }
        auto hierarchy = std::move(object.hierarchy_fields);
        if (object.parent.isValid() || !hierarchy.empty()) {
            hierarchy.emplace("Type", ArchiveValue{std::string("star::Hierarchy")});
            hierarchy.emplace("parent",
                              object.parent.isValid() ? ArchiveValue{object.parent.toString()} : ArchiveValue{});
            hierarchy.emplace("sibling_index", ArchiveValue{PlainScalar{std::to_string(object.sibling)}});
            components.push_back(ArchiveValue{std::move(hierarchy)});
        }
        if (!components.empty()) {
            fields.insert_or_assign("Components", ArchiveValue{std::move(components)});
        }
        objects.push_back(ArchiveValue{std::move(fields)});
    }
    document.fields.insert_or_assign("StarFile", ArchiveValue{std::string(kind)});
    document.fields.insert_or_assign("Version", ArchiveValue{PlainScalar{"1"}});
    document.fields.insert_or_assign("GameObjects", ArchiveValue{std::move(objects)});
    return std::move(document.fields);
}

std::expected<std::string, ArchiveError> writeDocument(SceneDocument document, std::string_view kind) {
    return writeYamlValue(ArchiveValue{encodeDocument(std::move(document), kind)});
}

bool applyOverride(ArchivedObject &object, const PrefabOverride &change) {
    if (change.path == "Name") {
        const auto *name = std::get_if<std::string>(&change.value.value);
        if (name == nullptr) {
            return false;
        }
        object.name = *name;
        return true;
    }
    constexpr std::string_view prefix = "Components/";
    if (!change.path.starts_with(prefix)) {
        return false;
    }
    const auto path = change.path.substr(prefix.size());
    const auto slash = path.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 == path.size()) {
        return false;
    }
    const auto type = path.substr(0, slash);
    const auto property = path.substr(slash + 1);
    if (type == "star::Hierarchy" || property == "Type" || property == "Version" ||
        property.find('/') != std::string_view::npos) {
        return false;
    }
    auto found = std::ranges::find(object.components, type, &ArchiveDocument::type_name);
    if (found == object.components.end()) {
        if (type != "star::Transform") {
            return false;
        }
        object.components.push_back({std::string(type), 1, {}});
        found = std::prev(object.components.end());
    }
    found->fields.insert_or_assign(std::string(property), change.value);
    if (type == "star::Transform" && (property == "position" || property == "scale" || property == "rotation")) {
        normalizeVector(found->fields, property, property == "rotation" ? "xyzw" : "xyz");
    }
    return true;
}

std::expected<LoadedScene, ArchiveError> restoreScene(World &world, SceneDocument document, LoadPolicy policy,
                                                      std::span<const reflect::TypeInfo *const> component_types,
                                                      SceneSnapshotRestore mode = SceneSnapshotRestore::Play) {
    const auto created = world.createScene();
    if (!created) {
        return failed();
    }
    auto &scene = **created;
    struct Rollback {
        World &world;
        Scene &scene;
        bool completed = false;
        ~Rollback() {
            if (!completed) {
                world.unloadScene(scene);
            }
        }
    } rollback{world, scene};

    for (const auto *type : component_types) {
        if (type == nullptr || !scene.registerComponent(*type)) {
            return failed();
        }
    }
    SceneArchiveAccess::sceneFields(scene) = std::move(document.fields);
    LoadedScene result{&scene, {}};
    if (auto restored = SceneArchiveAccess::restore(scene, document.objects, policy, result.diagnostics, mode);
        !restored) {
        return std::unexpected(std::move(restored.error()));
    }
    rollback.completed = true;
    return result;
}

} // namespace

std::expected<std::string, ArchiveError> saveSceneYaml(const Scene &scene) {
    try {
        auto records = SceneArchiveAccess::capture(scene, {});
        if (!records) {
            return std::unexpected(std::move(records.error()));
        }
        return writeDocument({SceneArchiveAccess::sceneFields(scene), std::move(*records)}, "Scene");
    } catch (...) {
        return failed();
    }
}

std::expected<LoadedScene, ArchiveError> loadSceneYaml(World &world, std::string_view source, LoadPolicy policy,
                                                       std::span<const reflect::TypeInfo *const> component_types) {
    try {
        auto document = readDocument(source, "Scene");
        if (!document) {
            return std::unexpected(std::move(document.error()));
        }
        return restoreScene(world, std::move(*document), policy, component_types);
    } catch (...) {
        return failed();
    }
}

std::expected<std::vector<std::byte>, ArchiveError> saveSceneBinary(const Scene &scene) {
    try {
        auto records = SceneArchiveAccess::capture(scene, {}, CapturePolicy::AllFields);
        if (!records) {
            return std::unexpected(std::move(records.error()));
        }
        return writeBinary(
            {"Scene", 1, encodeDocument({SceneArchiveAccess::sceneFields(scene), std::move(*records)}, "Scene")});
    } catch (...) {
        return failed();
    }
}

std::expected<LoadedScene, ArchiveError> loadSceneBinary(World &world, std::span<const std::byte> source,
                                                         LoadPolicy policy, SceneSnapshotRestore mode) {
    try {
        auto archive = readBinary(source);
        if (!archive) {
            return std::unexpected(std::move(archive.error()));
        }
        if (archive->type_name != "Scene" || archive->version != 1) {
            return invalid("/StarFile");
        }
        auto document = decodeDocument(ArchiveValue{std::move(archive->fields)}, "Scene");
        if (!document) {
            return std::unexpected(std::move(document.error()));
        }
        return restoreScene(world, std::move(*document), policy, {}, mode);
    } catch (...) {
        return failed();
    }
}

std::expected<std::string, ArchiveError> savePrefabYaml(const Scene &scene, ObjectHandle root) {
    try {
        if (root == ObjectHandle{}) {
            return invalid("/GameObjects");
        }
        auto objects = SceneArchiveAccess::capture(scene, root);
        if (!objects) {
            return std::unexpected(std::move(objects.error()));
        }
        if (std::ranges::any_of(*objects,
                                [](const auto &object) { return object.fields.contains("PrefabInstance"); })) {
            return invalid("/GameObjects/PrefabInstance");
        }
        return writeDocument({{}, std::move(*objects)}, "Prefab");
    } catch (...) {
        return failed();
    }
}

std::expected<PrefabInstance, ArchiveError> instantiatePrefabYaml(Scene &scene, std::string_view source,
                                                                  Guid prefab_asset,
                                                                  std::span<const PrefabOverride> overrides,
                                                                  LoadPolicy policy, bool preserve_guids) {
    try {
        if (!prefab_asset.isValid() || SceneArchiveAccess::isBusy(scene)) {
            return invalid("/Prefab");
        }
        auto document = readDocument(source, "Prefab");
        if (!document) {
            return std::unexpected(std::move(document.error()));
        }
        auto root =
            std::ranges::find_if(document->objects, [](const auto &object) { return !object.parent.isValid(); });
        Sequence edits;
        for (const auto &change : overrides) {
            auto object = std::ranges::find(document->objects, change.object, &ArchivedObject::guid);
            if (object == document->objects.end() || !applyOverride(*object, change)) {
                return invalid("/Overrides/" + std::string(change.path));
            }
            edits.push_back(ArchiveValue{Mapping{{"Object", ArchiveValue{change.object.toString()}},
                                                 {"Path", ArchiveValue{std::string(change.path)}},
                                                 {"Value", change.value}}});
        }
        if (!preserve_guids) {
            root->fields.insert_or_assign(
                "PrefabInstance", ArchiveValue{Mapping{{"Prefab", ArchiveValue{prefab_asset.toString()}},
                                                       {"RootGuidInPrefab", ArchiveValue{root->guid.toString()}},
                                                       {"Overrides", ArchiveValue{std::move(edits)}}}});
            std::map<Guid, Guid> remap;
            for (const auto &object : document->objects) {
                remap.emplace(object.guid, Guid::generate());
            }
            for (auto &object : document->objects) {
                object.guid = remap.at(object.guid);
                if (object.parent.isValid()) {
                    object.parent = remap.at(object.parent);
                }
            }
        }
        PrefabInstance result{};
        if (auto restored = SceneArchiveAccess::restore(scene, document->objects, policy, result.diagnostics);
            !restored) {
            return std::unexpected(std::move(restored.error()));
        }
        result.root = scene.findObject(root->guid)->getHandle();
        return result;
    } catch (...) {
        return failed();
    }
}

} // namespace star
