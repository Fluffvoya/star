#include "reflection/reflection.hpp"

#include <algorithm>

namespace star::reflect {

const TypeInfo *TypeRegistry::find(TypeId id) const noexcept {
    const auto found = m_types.find(id);
    return found == m_types.end() ? nullptr : found->second;
}

const TypeInfo *TypeRegistry::find(std::string_view name) const noexcept { return find(makeTypeId(name)); }

std::vector<const TypeInfo *> TypeRegistry::derivedFrom(const TypeInfo &base) const {
    std::vector<const TypeInfo *> result;
    for (const auto &[id, type] : m_types) {
        (void)id;
        for (const TypeInfo *current = type->base; current != nullptr; current = current->base) {
            if (current == &base) {
                result.push_back(type);
                break;
            }
        }
    }
    std::ranges::sort(result, [](const TypeInfo *left, const TypeInfo *right) { return left->name < right->name; });
    return result;
}

std::expected<void, RegistryError> TypeRegistry::registerModule(ModuleId owner,
                                                                std::span<const TypeInfo *const> types) {
    for (const TypeInfo *type : types) {
        if (type == nullptr || type->id == 0 || type->name.empty() || type->owner != owner) {
            return std::unexpected(RegistryError{RegistryErrorCode::InvalidType, type == nullptr ? 0 : type->id,
                                                 "reflection type metadata is invalid"});
        }
        if (const TypeInfo *existing = find(type->id); existing != nullptr && existing != type) {
            return std::unexpected(
                RegistryError{RegistryErrorCode::DuplicateType, type->id, "reflection type id is already registered"});
        }
    }
    for (const TypeInfo *type : types) {
        m_types.insert_or_assign(type->id, type);
    }
    return {};
}

void TypeRegistry::unregisterModule(ModuleId owner) noexcept {
    for (auto iterator = m_types.begin(); iterator != m_types.end();) {
        if (iterator->second->owner == owner) {
            iterator = m_types.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void TypeRegistry::clear() noexcept { m_types.clear(); }

} // namespace star::reflect
