#include "editor/command.hpp"

#include "core/log.hpp"
#include "scene/scene_archive.hpp"

#include <string>
#include <utility>

namespace star::editor {
namespace {

const LogCategory k_log_category("EditorCommands", LogLevel::Info);

[[nodiscard]] const reflect::PropertyInfo *findProperty(const reflect::TypeInfo &type, std::string_view name) {
    for (const reflect::PropertyInfo &property : type.properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

} // namespace

std::optional<PropertySlot> resolveProperty(Scene &scene, const Guid &object, reflect::TypeId component,
                                            std::span<const std::string> path) {
    if (path.empty()) {
        return std::nullopt;
    }
    GameObject *target = scene.findObject(object);
    if (target == nullptr) {
        return std::nullopt;
    }
    void *current = scene.findComponent(target->getHandle(), component);
    const reflect::TypeInfo *current_type = scene.findComponentType(component);
    if (current == nullptr || current_type == nullptr) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < path.size(); ++index) {
        const reflect::PropertyInfo *property = findProperty(*current_type, path[index]);
        if (property == nullptr || property->get_mutable == nullptr ||
            reflect::hasFlag(property->flags, reflect::PropertyFlags::ReadOnly)) {
            return std::nullopt;
        }
        void *member = property->get_mutable(current);
        if (member == nullptr) {
            return std::nullopt;
        }
        if (index + 1 == path.size()) {
            return PropertySlot{member, property->nestedType(), property};
        }
        current_type = property->nestedType();
        if (current_type == nullptr) {
            return std::nullopt;
        }
        current = member;
    }
    return std::nullopt;
}

std::string formatPropertyText(const PropertySlot &slot) {
    if (slot.info == nullptr || slot.info->format_scalar == nullptr || slot.value == nullptr) {
        return {};
    }
    return slot.info->format_scalar(slot.value);
}

bool writePropertyText(const PropertySlot &slot, std::string_view text) {
    if (slot.info == nullptr || slot.info->parse_scalar == nullptr || slot.value == nullptr) {
        return false;
    }
    return slot.info->parse_scalar(text, slot.value);
}

CreateObjectCommand::CreateObjectCommand(std::string name, Guid parent)
    : m_name(std::move(name)), m_parent(parent), m_guid(Guid::generate()) {}

std::string_view CreateObjectCommand::getName() const noexcept { return "Create Object"; }

bool CreateObjectCommand::execute(EditorDocument &document) {
    Scene &scene = document.getScene();
    auto created = scene.createObject(m_name, m_guid);
    if (!created) {
        return false;
    }
    if (m_parent.isValid()) {
        if (GameObject *parent = scene.findObject(m_parent); parent != nullptr) {
            static_cast<void>(scene.queueSetParent(*created, parent->getHandle(), true));
        }
    }
    static_cast<void>(scene.commitCommands());
    document.select(m_guid, false);
    document.markDirty();
    return true;
}

void CreateObjectCommand::undo(EditorDocument &document) {
    Scene &scene = document.getScene();
    if (GameObject *object = scene.findObject(m_guid); object != nullptr) {
        static_cast<void>(scene.queueDestroy(object->getHandle()));
        static_cast<void>(scene.commitCommands());
    }
    document.clearSelection();
    document.markDirty();
}

DeleteObjectCommand::DeleteObjectCommand(Guid object) : m_object(object) {}

std::string_view DeleteObjectCommand::getName() const noexcept { return "Delete Object"; }

bool DeleteObjectCommand::execute(EditorDocument &document) {
    Scene &scene = document.getScene();
    GameObject *object = scene.findObject(m_object);
    if (object == nullptr) {
        return false;
    }
    if (GameObject *parent = object->getParent(); parent != nullptr) {
        m_parent = parent->getGuid();
    }
    auto snapshot = savePrefabYaml(scene, object->getHandle());
    if (!snapshot) {
        log::warning(k_log_category, "Cannot snapshot the object before deleting it");
        return false;
    }
    m_snapshot = std::move(*snapshot);
    m_captured = true;
    m_prefab_asset = Guid::generate();
    static_cast<void>(scene.queueDestroy(object->getHandle()));
    static_cast<void>(scene.commitCommands());
    document.clearSelection();
    document.markDirty();
    return true;
}

void DeleteObjectCommand::undo(EditorDocument &document) {
    if (!m_captured) {
        return;
    }
    Scene &scene = document.getScene();
    auto restored =
        instantiatePrefabYaml(scene, m_snapshot, m_prefab_asset, {}, serialization::LoadPolicy::Preserve, true);
    if (!restored) {
        log::warning(k_log_category, "Cannot restore the deleted object");
        return;
    }
    if (m_parent.isValid()) {
        if (GameObject *parent = scene.findObject(m_parent); parent != nullptr) {
            static_cast<void>(scene.queueSetParent(restored->root, parent->getHandle(), true));
        }
    }
    static_cast<void>(scene.commitCommands());
    if (GameObject *object = scene.findObject(restored->root); object != nullptr) {
        document.select(object->getGuid(), false);
    }
    document.markDirty();
}

RenameObjectCommand::RenameObjectCommand(Guid object, std::string name) : m_object(object), m_name(std::move(name)) {}

std::string_view RenameObjectCommand::getName() const noexcept { return "Rename Object"; }

bool RenameObjectCommand::execute(EditorDocument &document) {
    GameObject *object = document.getScene().findObject(m_object);
    if (object == nullptr) {
        return false;
    }
    if (!m_has_before) {
        m_before = object->getName();
        m_has_before = true;
    }
    object->setName(m_name);
    document.markDirty();
    return true;
}

void RenameObjectCommand::undo(EditorDocument &document) {
    if (!m_has_before) {
        return;
    }
    if (GameObject *object = document.getScene().findObject(m_object); object != nullptr) {
        object->setName(m_before);
        document.markDirty();
    }
}

SetPropertyCommand::SetPropertyCommand(Guid object, reflect::TypeId component, std::vector<std::string> path,
                                       std::string text)
    : m_object(object), m_component(component), m_path(std::move(path)), m_text(std::move(text)) {}

std::string_view SetPropertyCommand::getName() const noexcept { return "Edit Property"; }

bool SetPropertyCommand::apply(EditorDocument &document, std::string_view text) {
    const auto slot = resolveProperty(document.getScene(), m_object, m_component, m_path);
    if (!slot) {
        return false;
    }
    return writePropertyText(*slot, text);
}

bool SetPropertyCommand::execute(EditorDocument &document) {
    if (!m_has_before) {
        const auto slot = resolveProperty(document.getScene(), m_object, m_component, m_path);
        if (!slot) {
            return false;
        }
        m_before = formatPropertyText(*slot);
        m_has_before = true;
    }
    if (!apply(document, m_text)) {
        return false;
    }
    document.markDirty();
    return true;
}

void SetPropertyCommand::undo(EditorDocument &document) {
    if (!m_has_before) {
        return;
    }
    if (apply(document, m_before)) {
        document.markDirty();
    }
}

} // namespace star::editor
