#pragma once

#include "editor/document.hpp"
#include "editor/export.hpp"
#include "scene/components.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace star::editor {

class STAR_EDITORCORE_API EditorCommand {
  public:
    virtual ~EditorCommand() = default;
    [[nodiscard]] virtual std::string_view getName() const noexcept = 0;
    [[nodiscard]] virtual bool execute(EditorDocument &document) = 0;
    virtual void undo(EditorDocument &document) = 0;
};

// A resolved component property: the pointer to write and its metadata.
struct PropertySlot {
    void *value = nullptr;
    const reflect::TypeInfo *type = nullptr;
    const reflect::PropertyInfo *info = nullptr;
};

// Walks a property path such as {"position", "x"} inside one component.
[[nodiscard]] STAR_EDITORCORE_API std::optional<PropertySlot>
resolveProperty(Scene &scene, const Guid &object, reflect::TypeId component, std::span<const std::string> path);

[[nodiscard]] STAR_EDITORCORE_API std::string formatPropertyText(const PropertySlot &slot);
[[nodiscard]] STAR_EDITORCORE_API bool writePropertyText(const PropertySlot &slot, std::string_view text);

class STAR_EDITORCORE_API CreateObjectCommand final : public EditorCommand {
  public:
    CreateObjectCommand(std::string name, Guid parent = {});
    [[nodiscard]] std::string_view getName() const noexcept override;
    [[nodiscard]] bool execute(EditorDocument &document) override;
    void undo(EditorDocument &document) override;
    [[nodiscard]] const Guid &getCreatedGuid() const noexcept { return m_guid; }

  private:
    std::string m_name;
    Guid m_parent;
    Guid m_guid;
};

class STAR_EDITORCORE_API DeleteObjectCommand final : public EditorCommand {
  public:
    explicit DeleteObjectCommand(Guid object);
    [[nodiscard]] std::string_view getName() const noexcept override;
    [[nodiscard]] bool execute(EditorDocument &document) override;
    void undo(EditorDocument &document) override;

  private:
    Guid m_object;
    Guid m_parent;
    Guid m_prefab_asset;
    std::string m_snapshot;
    bool m_captured = false;
};

class STAR_EDITORCORE_API RenameObjectCommand final : public EditorCommand {
  public:
    RenameObjectCommand(Guid object, std::string name);
    [[nodiscard]] std::string_view getName() const noexcept override;
    [[nodiscard]] bool execute(EditorDocument &document) override;
    void undo(EditorDocument &document) override;

  private:
    Guid m_object;
    std::string m_name;
    std::string m_before;
    bool m_has_before = false;
};

class STAR_EDITORCORE_API SetPropertyCommand final : public EditorCommand {
  public:
    SetPropertyCommand(Guid object, reflect::TypeId component, std::vector<std::string> path, std::string text);
    [[nodiscard]] std::string_view getName() const noexcept override;
    [[nodiscard]] bool execute(EditorDocument &document) override;
    void undo(EditorDocument &document) override;

  private:
    [[nodiscard]] bool apply(EditorDocument &document, std::string_view text);

    Guid m_object;
    reflect::TypeId m_component;
    std::vector<std::string> m_path;
    std::string m_text;
    std::string m_before;
    bool m_has_before = false;
};

class STAR_EDITORCORE_API SetTransformCommand final : public EditorCommand {
  public:
    SetTransformCommand(Guid object, Transform transform);
    [[nodiscard]] std::string_view getName() const noexcept override;
    [[nodiscard]] bool execute(EditorDocument &document) override;
    void undo(EditorDocument &document) override;

  private:
    Guid m_object;
    Transform m_transform;
    std::optional<Transform> m_before;
};

} // namespace star::editor
