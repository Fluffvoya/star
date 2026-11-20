#include "reflection/reflection.hpp"

#include <xxhash.h>

#include <algorithm>

namespace star::reflect {

TypeId makeTypeId(std::string_view name) noexcept { return XXH3_64bits(name.data(), name.size()); }

const TypeInfo *TypeRegistry::find(TypeId id) const noexcept {
    const auto found = m_types.find(id);
    return found == m_types.end() ? nullptr : found->second;
}

const TypeInfo *TypeRegistry::find(std::string_view name) const noexcept {
    const auto *type = find(makeTypeId(name));
    return type != nullptr && (type->name == name || type->former_name == name) ? type : nullptr;
}

std::vector<const TypeInfo *> TypeRegistry::derivedFrom(const TypeInfo &base) const {
    std::vector<const TypeInfo *> result;
    for (const auto &[id, type] : m_types) {
        if (id != type->id) {
            continue;
        }
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
                                                                std::span<const TypeInfo *const> types) try {
    auto replacement = m_types;
    for (const TypeInfo *type : types) {
        if (type == nullptr || type->id == 0 || type->name.empty() || type->id != makeTypeId(type->name) ||
            type->version == 0) {
            return std::unexpected(RegistryError{RegistryErrorCode::InvalidType, type == nullptr ? 0 : type->id,
                                                 "reflection type metadata is invalid"});
        }
        if (type->owner != owner) {
            return std::unexpected(
                RegistryError{RegistryErrorCode::TypeOwnerMismatch, type->id, "reflection type owner does not match"});
        }
        const auto insert = [&](TypeId id) { return replacement.try_emplace(id, type).first->second == type; };
        if (!insert(type->id) || (!type->former_name.empty() && !insert(makeTypeId(type->former_name)))) {
            return std::unexpected(RegistryError{RegistryErrorCode::DuplicateType, type->id,
                                                 "reflection type name or ID is already registered"});
        }
    }
    m_types.swap(replacement);
    return {};
} catch (...) {
    return std::unexpected(RegistryError{RegistryErrorCode::AllocationFailed, 0, {}});
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
