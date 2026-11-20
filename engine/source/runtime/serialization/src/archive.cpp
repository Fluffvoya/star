#include "archive_value.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <utility>

namespace star::serialization {

struct ArchiveAccess {
    static Archive make(bool loading, void *context, Archive::Transfer transfer) {
        return Archive(loading, context, transfer);
    }
};

namespace {

struct ObjectDeleter {
    const reflect::TypeInfo &type;
    std::size_t alignment;
    bool constructed = false;

    void operator()(void *object) const {
        if (constructed) {
            type.ops.destroy(object);
        }
        ::operator delete(object, std::align_val_t(alignment));
    }
};

using WorkingObject = std::unique_ptr<void, ObjectDeleter>;

WorkingObject allocateObject(const reflect::TypeInfo &type) {
    const auto alignment = std::max(type.alignment, alignof(std::max_align_t));
    return WorkingObject(::operator new(type.size, std::align_val_t(alignment)), ObjectDeleter{type, alignment});
}

bool hasCopyOperations(const reflect::TypeInfo &type) {
    return type.size != 0 && std::has_single_bit(type.alignment) && type.ops.copy_construct != nullptr &&
           type.ops.destroy != nullptr;
}

template <typename T> bool parseNumber(std::string_view text, T &value, int base) {
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::string fieldPath(std::string_view parent, std::string_view field) {
    return std::string(parent) + "/" + std::string(field);
}

bool isEnum(const reflect::TypeInfo *type) { return type != nullptr && type->kind == reflect::TypeKind::Enum; }

const ArchiveValue::Mapping *fieldExtensions(const reflect::PropertyInfo &property, const ArchiveValue &value) {
    const auto *type = property.nestedType();
    if (type == nullptr || isEnum(type) || property.container != nullptr) {
        return nullptr;
    }
    return std::get_if<ArchiveValue::Mapping>(&value.value);
}

ValueResult captureFields(const reflect::TypeInfo &type, const void *object, const ArchiveValue::Mapping &extensions,
                          std::string_view path, unsigned depth);

ValueResult captureValue(const reflect::PropertyInfo &property, const void *value,
                         const ArchiveValue::Mapping *extensions, std::string_view path, unsigned depth) {
    if (depth > max_archive_depth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::string(path)});
    }
    const auto *nested_type = property.nestedType();
    if (const auto *ops = property.container) {
        struct Context {
            reflect::PropertyInfo element;
            std::string_view path;
            unsigned depth;
            bool is_mapping;
            ArchiveValue::Sequence sequence;
            ArchiveValue::Mapping mapping;
            std::optional<ArchiveError> failure;
        } context{ops->element(nested_type), path, depth, ops->is_mapping, {}, {}, {}};
        if (!ops->is_mapping) {
            context.sequence.reserve(ops->size(value));
        }
        ops->visit(value, &context, [](void *opaque, std::string_view key, const void *entry) {
            auto &state = *static_cast<Context *>(opaque);
            if (state.failure) {
                return;
            }
            const auto index = state.is_mapping ? std::string(key) : std::to_string(state.sequence.size());
            auto captured = captureValue(state.element, entry, nullptr, fieldPath(state.path, index), state.depth + 1);
            if (!captured) {
                state.failure = std::move(captured.error());
            } else if (state.is_mapping) {
                state.mapping.emplace(key, std::move(*captured));
            } else {
                state.sequence.push_back(std::move(*captured));
            }
        });
        if (context.failure) {
            return std::unexpected(std::move(*context.failure));
        }
        return context.is_mapping ? ArchiveValue{std::move(context.mapping)}
                                  : ArchiveValue{std::move(context.sequence)};
    }
    if (isEnum(nested_type)) {
        const auto &type = *nested_type;
        if (type.enum_ops.get != nullptr) {
            const auto current = type.enum_ops.get(value);
            const auto entry = std::ranges::find(type.enum_entries, current, &reflect::EnumEntry::value);
            if (entry != type.enum_entries.end()) {
                return ArchiveValue{std::string(entry->name)};
            }
        }
    } else if (nested_type != nullptr && property.kind == reflect::PropertyKind::Reflected) {
        const ArchiveValue::Mapping empty;
        return captureFields(*nested_type, value, extensions == nullptr ? empty : *extensions, path, depth + 1);
    } else if (property.format_scalar != nullptr) {
        auto text = property.format_scalar(value);
        return property.kind == reflect::PropertyKind::String ? ArchiveValue{std::move(text)}
                                                              : ArchiveValue{PlainScalar{std::move(text)}};
    }
    return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, std::string(path)});
}

ValueResult captureCustom(const reflect::TypeInfo &type, const void *object, const ArchiveValue::Mapping &extensions,
                          std::string_view path, unsigned depth) {
    if (!hasCopyOperations(type)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, std::string(path)});
    }
    auto working = allocateObject(type);
    type.ops.copy_construct(working.get(), object);
    working.get_deleter().constructed = true;
    struct Context {
        ArchiveValue::Mapping fields;
        std::set<std::string, std::less<>> names;
        std::string_view path;
        unsigned depth;
        std::optional<ArchiveError> failure;
    } context{extensions, {}, path, depth, {}};
    auto archive = ArchiveAccess::make(
        false, &context, [](void *opaque, std::string_view name, const reflect::PropertyInfo &property, void *value) {
            auto &state = *static_cast<Context *>(opaque);
            if (state.failure) {
                return false;
            }
            const auto field_path = fieldPath(state.path, name);
            if (name.empty() || !state.names.emplace(name).second) {
                state.failure = ArchiveError{ArchiveErrorCode::InvalidInput, field_path};
                return false;
            }
            const auto existing = state.fields.find(name);
            const auto *nested = existing == state.fields.end() ? nullptr : fieldExtensions(property, existing->second);
            if (existing != state.fields.end() && nested == nullptr) {
                return true;
            }
            auto saved = captureValue(property, value, nested, field_path, state.depth);
            if (!saved) {
                state.failure = std::move(saved.error());
                return false;
            }
            state.fields.insert_or_assign(std::string(name), std::move(*saved));
            return true;
        });
    type.serialize(working.get(), archive);
    if (context.failure) {
        return std::unexpected(std::move(*context.failure));
    }
    return ArchiveValue{std::move(context.fields)};
}

ValueResult captureFields(const reflect::TypeInfo &type, const void *object, const ArchiveValue::Mapping &extensions,
                          std::string_view path, unsigned depth) {
    if (depth > max_archive_depth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::string(path)});
    }
    if (type.serialize != nullptr) {
        return captureCustom(type, object, extensions, path, depth);
    }
    ArchiveValue::Mapping fields = extensions;
    if (type.base != nullptr && type.base_access.get_const != nullptr) {
        auto base = captureFields(*type.base, type.base_access.get_const(object), fields, path, depth + 1);
        if (!base) {
            return base;
        }
        fields = std::get<ArchiveValue::Mapping>(std::move(base->value));
        for (const auto &property : type.properties) {
            if (!extensions.contains(property.name)) {
                fields.erase(std::string(property.name));
            }
        }
    }
    for (const auto &property : type.properties) {
        if (reflect::hasFlag(property.flags, reflect::PropertyFlags::Transient)) {
            fields.erase(std::string(property.name));
            if (!property.former_name.empty()) {
                fields.erase(std::string(property.former_name));
            }
            continue;
        }
        const auto existing = fields.find(property.name);
        const auto *nested_extensions =
            existing == fields.end() ? nullptr : fieldExtensions(property, existing->second);
        if (existing != fields.end() && nested_extensions == nullptr) {
            continue;
        }
        if (property.get_const == nullptr) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, fieldPath(path, property.name)});
        }
        auto captured = captureValue(property, property.get_const(object), nested_extensions,
                                     fieldPath(path, property.name), depth);
        if (!captured) {
            return captured;
        }
        fields.insert_or_assign(std::string(property.name), std::move(*captured));
    }
    return ArchiveValue{std::move(fields)};
}

struct PropertyMatch {
    const reflect::PropertyInfo *property;
    void *owner;
};

PropertyMatch findProperty(const reflect::TypeInfo &type, void *object, std::string_view name) {
    PropertyMatch former{};
    const auto *current = &type;
    for (unsigned depth = 0; current != nullptr && depth <= max_archive_depth; ++depth) {
        for (const auto &property : current->properties) {
            if (property.name == name) {
                return {&property, object};
            }
            if (former.property == nullptr && !name.empty() && property.former_name == name) {
                former = {&property, object};
            }
        }
        if (current->base == nullptr || current->base_access.get_mutable == nullptr) {
            break;
        }
        object = current->base_access.get_mutable(object);
        current = current->base;
    }
    return former;
}

using ApplyResult = std::expected<void, ArchiveError>;

ApplyResult applyFields(const ArchiveValue::Mapping &fields, const reflect::TypeInfo &type, void *object,
                        LoadPolicy policy, LoadResult &result, std::string_view path, unsigned depth);

ApplyResult applyValue(const reflect::PropertyInfo &property, const ArchiveValue &value, void *member,
                       LoadPolicy policy, LoadResult &result, std::string_view path, unsigned depth) {
    if (depth > max_archive_depth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::string(path)});
    }
    const auto *nested_type = property.nestedType();
    if (const auto *ops = property.container) {
        const auto *mapping = std::get_if<ArchiveValue::Mapping>(&value.value);
        const auto *sequence = std::get_if<ArchiveValue::Sequence>(&value.value);
        if ((ops->is_mapping && mapping == nullptr) || (!ops->is_mapping && sequence == nullptr)) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::TypeMismatch, std::string(path)});
        }
        struct Context {
            const reflect::ContainerOps &ops;
            reflect::PropertyInfo element;
            const ArchiveValue::Mapping *mapping;
            const ArchiveValue::Sequence *sequence;
            std::string_view path;
            unsigned depth;
            ApplyResult status{};
        } context{*ops, ops->element(nested_type), mapping, sequence, path, depth};
        const auto count = ops->is_mapping ? mapping->size() : sequence->size();
        if (ops->replace(member, count, &context, [](void *replacement, void *opaque) {
                auto &state = *static_cast<Context *>(opaque);
                std::size_t index = 0;
                auto apply_entry = [&](std::string_view key, const ArchiveValue &entry) {
                    const auto label = state.mapping == nullptr ? std::to_string(index) : std::string(key);
                    void *target = state.ops.write(replacement, index++, key);
                    LoadResult nested;
                    state.status = applyValue(state.element, entry, target, LoadPolicy::Strict, nested,
                                              fieldPath(state.path, label), state.depth + 1);
                    return state.status.has_value();
                };
                if (state.mapping != nullptr) {
                    for (const auto &[key, entry] : *state.mapping) {
                        if (!apply_entry(key, entry)) {
                            return false;
                        }
                    }
                } else {
                    for (const auto &entry : *state.sequence) {
                        if (!apply_entry({}, entry)) {
                            return false;
                        }
                    }
                }
                return true;
            })) {
            return {};
        }
        if (!context.status) {
            return std::move(context.status);
        }
        return std::unexpected(ArchiveError{ArchiveErrorCode::TypeMismatch, std::string(path)});
    }
    if (isEnum(nested_type)) {
        const auto *text = std::get_if<std::string>(&value.value);
        const auto &type = *nested_type;
        if (text != nullptr && type.enum_ops.set != nullptr) {
            const auto entry = std::ranges::find(type.enum_entries, *text, &reflect::EnumEntry::name);
            if (entry != type.enum_entries.end()) {
                type.enum_ops.set(member, entry->value);
                return {};
            }
        }
    } else if (nested_type != nullptr && property.kind == reflect::PropertyKind::Reflected) {
        if (const auto *mapping = std::get_if<ArchiveValue::Mapping>(&value.value)) {
            return applyFields(*mapping, *nested_type, member, policy, result, path, depth + 1);
        }
    } else if (property.parse_scalar != nullptr) {
        if (property.kind == reflect::PropertyKind::String) {
            if (const auto *text = std::get_if<std::string>(&value.value);
                text != nullptr && property.parse_scalar(*text, member)) {
                return {};
            }
        } else if (const auto *text = std::get_if<PlainScalar>(&value.value);
                   text != nullptr && property.parse_scalar(text->text, member)) {
            return {};
        }
    } else {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, std::string(path)});
    }
    return std::unexpected(ArchiveError{ArchiveErrorCode::TypeMismatch, std::string(path)});
}

ApplyResult applyCustom(const ArchiveValue::Mapping &fields, const reflect::TypeInfo &type, void *object,
                        LoadPolicy policy, LoadResult &result, std::string_view path, unsigned depth) {
    struct Context {
        const ArchiveValue::Mapping &fields;
        std::set<std::string, std::less<>> names;
        LoadPolicy policy;
        LoadResult &result;
        std::string_view path;
        unsigned depth;
        ApplyResult failure{};
    } context{fields, {}, policy, result, path, depth};
    auto archive = ArchiveAccess::make(
        true, &context, [](void *opaque, std::string_view name, const reflect::PropertyInfo &property, void *member) {
            auto &state = *static_cast<Context *>(opaque);
            if (!state.failure) {
                return false;
            }
            const auto field_path = fieldPath(state.path, name);
            if (name.empty() || !state.names.emplace(name).second) {
                state.failure = std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, field_path});
                return false;
            }
            const auto field = state.fields.find(name);
            if (field == state.fields.end()) {
                return true;
            }

            LoadResult nested;
            auto applied = applyValue(property, field->second, member, state.policy, nested, field_path, state.depth);
            if (applied) {
                if (!nested.extensions.empty()) {
                    state.result.extensions.emplace(name, ArchiveValue{std::move(nested.extensions)});
                }
                for (auto &diagnostic : nested.diagnostics) {
                    state.result.diagnostics.push_back(std::move(diagnostic));
                }
            } else if (state.policy == LoadPolicy::Strict) {
                state.failure = std::unexpected(std::move(applied.error()));
            } else {
                state.result.extensions.emplace(name, field->second);
                state.result.diagnostics.push_back(std::move(applied.error()));
            }
            return applied.has_value();
        });
    type.serialize(object, archive);
    if (!context.failure) {
        return context.failure;
    }
    for (const auto &[name, value] : fields) {
        if (context.names.contains(name)) {
            continue;
        }
        ArchiveError diagnostic{ArchiveErrorCode::UnknownField, fieldPath(path, name)};
        if (policy == LoadPolicy::Strict) {
            return std::unexpected(std::move(diagnostic));
        }
        result.extensions.emplace(name, value);
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return {};
}

std::expected<void, ArchiveError> applyFields(const ArchiveValue::Mapping &fields, const reflect::TypeInfo &type,
                                              void *object, LoadPolicy policy, LoadResult &result,
                                              std::string_view path, unsigned depth) {
    if (depth > max_archive_depth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::string(path)});
    }
    if (type.serialize != nullptr) {
        return applyCustom(fields, type, object, policy, result, path, depth);
    }
    for (const auto &[name, value] : fields) {
        auto [property, owner] = findProperty(type, object, name);
        const auto member_path = fieldPath(path, name);
        if (property != nullptr && reflect::hasFlag(property->flags, reflect::PropertyFlags::Transient)) {
            continue;
        }
        if (property != nullptr && name != property->name && fields.contains(property->name)) {
            property = nullptr;
        }
        ArchiveError error{ArchiveErrorCode::UnknownField, member_path};
        if (property != nullptr && property->get_mutable != nullptr) {
            LoadResult nested;
            auto applied =
                applyValue(*property, value, property->get_mutable(owner), policy, nested, member_path, depth);
            if (applied) {
                if (!nested.extensions.empty()) {
                    result.extensions.emplace(property->name, ArchiveValue{std::move(nested.extensions)});
                }
                for (auto &diagnostic : nested.diagnostics) {
                    result.diagnostics.push_back(std::move(diagnostic));
                }
                continue;
            }
            error = std::move(applied.error());
        } else if (property != nullptr) {
            error.code = ArchiveErrorCode::UnsupportedProperty;
        }
        if (policy == LoadPolicy::Strict) {
            return std::unexpected(std::move(error));
        }
        result.extensions.emplace(property == nullptr ? std::string_view(name) : property->name, value);
        result.diagnostics.push_back(std::move(error));
    }
    return {};
}

} // namespace

bool isPlainScalar(std::string_view text) {
    if (text == "true" || text == "false" || text == "True" || text == "False" || text == "TRUE" || text == "FALSE" ||
        text == ".inf" || text == "+.inf" || text == "-.inf" || text == ".nan" || text == ".Inf" || text == "+.Inf" ||
        text == "-.Inf" || text == ".NaN" || text == ".INF" || text == "+.INF" || text == "-.INF" || text == ".NAN") {
        return true;
    }
    if (text.starts_with("0x") || text.starts_with("0o")) {
        const auto digits = text.substr(2);
        const auto alphabet = text[1] == 'x' ? "0123456789abcdefABCDEF" : "01234567";
        return !digits.empty() && digits.find_first_not_of(alphabet) == std::string_view::npos;
    }
    if (text.starts_with('+') || text.starts_with('-')) {
        text.remove_prefix(1);
    }
    const auto digits = [](std::string_view &remaining) {
        const auto count = remaining.find_first_not_of("0123456789");
        const auto size = count == std::string_view::npos ? remaining.size() : count;
        remaining.remove_prefix(size);
        return size != 0;
    };
    bool has_digits = digits(text);
    if (text.starts_with('.')) {
        text.remove_prefix(1);
        has_digits = digits(text) || has_digits;
    }
    if (text.starts_with('e') || text.starts_with('E')) {
        text.remove_prefix(1);
        if (text.starts_with('+') || text.starts_with('-')) {
            text.remove_prefix(1);
        }
        has_digits = digits(text) && has_digits;
    }
    return has_digits && text.empty();
}

ArchiveValue documentValue(const ArchiveDocument &document) {
    ArchiveValue::Mapping envelope;
    envelope.emplace("Type", ArchiveValue{document.type_name});
    if (document.version != 1) {
        envelope.emplace("Version", ArchiveValue{PlainScalar{std::to_string(document.version)}});
    }
    envelope.emplace("Properties", ArchiveValue{document.fields});
    return ArchiveValue{std::move(envelope)};
}

std::expected<ArchiveDocument, ArchiveError> parseDocument(ArchiveValue root) {
    auto *envelope = std::get_if<ArchiveValue::Mapping>(&root.value);
    if (envelope == nullptr || !envelope->contains("Type") || !envelope->contains("Properties") ||
        envelope->size() != (envelope->contains("Version") ? 3u : 2u)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    }
    auto *name = std::get_if<std::string>(&envelope->at("Type").value);
    auto *fields = std::get_if<ArchiveValue::Mapping>(&envelope->at("Properties").value);
    if (name == nullptr || name->empty() || fields == nullptr) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    }
    std::uint32_t version = 1;
    if (const auto entry = envelope->find("Version"); entry != envelope->end()) {
        const auto *scalar = std::get_if<PlainScalar>(&entry->second.value);
        if (scalar == nullptr || !parseNumber(scalar->text, version, 10) || version == 0) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/Version"});
        }
    }
    return ArchiveDocument{std::move(*name), version, std::move(*fields)};
}

bool validateValue(const ArchiveValue &value, unsigned depth) {
    if (depth > max_archive_depth || value.value.valueless_by_exception()) {
        return false;
    }
    if (const auto *plain = std::get_if<PlainScalar>(&value.value)) {
        return isPlainScalar(plain->text);
    }
    if (const auto *sequence = std::get_if<ArchiveValue::Sequence>(&value.value)) {
        for (const auto &entry : *sequence) {
            if (!validateValue(entry, depth + 1)) {
                return false;
            }
        }
    }
    if (const auto *mapping = std::get_if<ArchiveValue::Mapping>(&value.value)) {
        for (const auto &[key, entry] : *mapping) {
            if (!validateValue(entry, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

std::expected<ArchiveDocument, ArchiveError> captureObject(const reflect::TypeInfo &type, const void *object,
                                                           const ArchiveValue::Mapping &extensions,
                                                           CapturePolicy policy) {
    if (object == nullptr || type.name.empty() || type.version == 0) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    }
    try {
        auto fields = captureFields(type, object, extensions, "", 0);
        if (!fields) {
            return std::unexpected(std::move(fields.error()));
        }
        auto mapping = std::get<ArchiveValue::Mapping>(std::move(fields->value));
        if (policy == CapturePolicy::OmitDefaults) {
            if (type.size == 0 || !std::has_single_bit(type.alignment) || type.ops.construct == nullptr ||
                type.ops.destroy == nullptr) {
                return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, "/"});
            }
            auto default_value = allocateObject(type);
            type.ops.construct(default_value.get());
            default_value.get_deleter().constructed = true;
            auto defaults = captureFields(type, default_value.get(), {}, "", 0);
            if (!defaults) {
                return std::unexpected(std::move(defaults.error()));
            }
            const auto &default_fields = std::get<ArchiveValue::Mapping>(defaults->value);
            std::erase_if(mapping, [&](const auto &entry) {
                const auto found = default_fields.find(entry.first);
                return !extensions.contains(entry.first) && found != default_fields.end() &&
                       found->second == entry.second;
            });
        }
        return ArchiveDocument{std::string(type.name), type.version, std::move(mapping)};
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

std::expected<LoadResult, ArchiveError> applyObject(const ArchiveDocument &document, const reflect::TypeInfo &type,
                                                    void *object, LoadPolicy policy, UpgradeFields upgrade) {
    if (object == nullptr || document.version == 0 || type.version == 0 || !hasCopyOperations(type) ||
        type.ops.swap == nullptr) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    }
    if (document.type_name != type.name && (type.former_name.empty() || document.type_name != type.former_name)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::TypeMismatch, "/Type"});
    }
    if (document.version > type.version) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedVersion, "/Version"});
    }
    try {
        const auto *fields = &document.fields;
        ArchiveValue::Mapping migrated;
        if (document.version < type.version && upgrade != nullptr) {
            migrated = document.fields;
            if (!upgrade(migrated, document.version, type.version)) {
                return std::unexpected(ArchiveError{ArchiveErrorCode::MigrationFailed, "/Version"});
            }
            fields = &migrated;
        }

        auto working = allocateObject(type);
        type.ops.copy_construct(working.get(), object);
        working.get_deleter().constructed = true;
        LoadResult result;
        auto applied = applyFields(*fields, type, working.get(), policy, result, "", 0);
        if (!applied) {
            return std::unexpected(std::move(applied.error()));
        }
        type.ops.swap(object, working.get());
        return result;
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

} // namespace star::serialization
