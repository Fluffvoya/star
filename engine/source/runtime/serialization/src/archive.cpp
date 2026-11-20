#include "archive_value.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <memory>
#include <new>
#include <utility>

namespace star::serialization {

namespace {

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

ValueResult captureFields(const reflect::TypeInfo &type, const void *object, const ArchiveValue::Mapping &extensions,
                          std::string_view path, unsigned depth) {
    if (depth > max_archive_depth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::string(path)});
    }
    ArchiveValue::Mapping fields = extensions;
    for (const auto &property : type.properties) {
        if (reflect::hasFlag(property.flags, reflect::PropertyFlags::Transient)) {
            fields.erase(std::string(property.name));
            if (!property.former_name.empty()) {
                fields.erase(std::string(property.former_name));
            }
            continue;
        }
        auto existing = fields.find(property.name);
        const auto *nested_extensions =
            existing == fields.end() ? nullptr : std::get_if<ArchiveValue::Mapping>(&existing->second.value);
        if (existing != fields.end() && (property.nested_type == nullptr || nested_extensions == nullptr)) {
            continue;
        }
        if (property.get_const == nullptr) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, fieldPath(path, property.name)});
        }
        const void *member = property.get_const(object);
        if (property.nested_type != nullptr && property.kind == reflect::PropertyKind::Reflected) {
            const ArchiveValue::Mapping empty;
            auto nested =
                captureFields(*property.nested_type, member, nested_extensions == nullptr ? empty : *nested_extensions,
                              fieldPath(path, property.name), depth + 1);
            if (!nested) {
                return std::unexpected(std::move(nested.error()));
            }
            fields.insert_or_assign(std::string(property.name), std::move(*nested));
        } else if (property.format_scalar != nullptr) {
            std::string text = property.format_scalar(member);
            ArchiveValue value = property.kind == reflect::PropertyKind::String
                                     ? ArchiveValue{std::move(text)}
                                     : ArchiveValue{PlainScalar{std::move(text)}};
            fields.emplace(property.name, std::move(value));
        } else {
            return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedProperty, fieldPath(path, property.name)});
        }
    }
    return ArchiveValue{std::move(fields)};
}

const reflect::PropertyInfo *findProperty(const reflect::TypeInfo &type, std::string_view name) {
    for (const auto &property : type.properties) {
        if (property.name == name) {
            return &property;
        }
    }
    for (const auto &property : type.properties) {
        if (property.former_name == name && !name.empty()) {
            return &property;
        }
    }
    return nullptr;
}

std::expected<void, ArchiveError> applyFields(const ArchiveValue::Mapping &fields, const reflect::TypeInfo &type,
                                              void *object, LoadPolicy policy, LoadResult &result,
                                              std::string_view path, unsigned depth) {
    if (depth > max_archive_depth) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, std::string(path)});
    }
    for (const auto &[name, value] : fields) {
        const auto *property = findProperty(type, name);
        const auto member_path = fieldPath(path, name);
        auto code = ArchiveErrorCode::UnknownField;
        if (property != nullptr) {
            if (reflect::hasFlag(property->flags, reflect::PropertyFlags::Transient)) {
                continue;
            }
            if (name != property->name && fields.contains(property->name)) {
                property = nullptr;
            }
        }
        if (property != nullptr) {
            code = ArchiveErrorCode::UnsupportedProperty;
            if (property->get_mutable != nullptr) {
                void *member = property->get_mutable(object);
                if (property->nested_type != nullptr && property->kind == reflect::PropertyKind::Reflected) {
                    if (const auto *mapping = std::get_if<ArchiveValue::Mapping>(&value.value)) {
                        LoadResult nested;
                        auto applied = applyFields(*mapping, *property->nested_type, member, policy, nested,
                                                   member_path, depth + 1);
                        if (!applied) {
                            return applied;
                        }
                        if (!nested.extensions.empty()) {
                            result.extensions.emplace(property->name, ArchiveValue{std::move(nested.extensions)});
                        }
                        for (auto &diagnostic : nested.diagnostics) {
                            result.diagnostics.push_back(std::move(diagnostic));
                        }
                        continue;
                    }
                    code = ArchiveErrorCode::TypeMismatch;
                } else if (property->parse_scalar != nullptr) {
                    const auto *plain = std::get_if<PlainScalar>(&value.value);
                    const auto *text = std::get_if<std::string>(&value.value);
                    if (property->kind == reflect::PropertyKind::String && text != nullptr) {
                        if (property->parse_scalar(*text, member)) {
                            continue;
                        }
                    } else if (property->kind != reflect::PropertyKind::String && plain != nullptr &&
                               property->parse_scalar(plain->text, member)) {
                        continue;
                    }
                    code = ArchiveErrorCode::TypeMismatch;
                }
            }
        }
        if (policy == LoadPolicy::Strict) {
            return std::unexpected(ArchiveError{code, member_path});
        }
        result.extensions.emplace(property == nullptr ? std::string_view(name) : property->name, value);
        result.diagnostics.push_back(ArchiveError{code, member_path});
    }
    return {};
}

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
                                                           const ArchiveValue::Mapping &extensions) {
    if (object == nullptr || type.name.empty() || type.version == 0) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    }
    try {
        auto fields = captureFields(type, object, extensions, "", 0);
        if (!fields) {
            return std::unexpected(std::move(fields.error()));
        }
        return ArchiveDocument{std::string(type.name), type.version,
                               std::get<ArchiveValue::Mapping>(std::move(fields->value))};
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

std::expected<LoadResult, ArchiveError> applyObject(const ArchiveDocument &document, const reflect::TypeInfo &type,
                                                    void *object, LoadPolicy policy, UpgradeFields upgrade) {
    if (object == nullptr || document.version == 0 || type.version == 0 || type.size == 0 ||
        !std::has_single_bit(type.alignment) || type.ops.copy_construct == nullptr || type.ops.destroy == nullptr ||
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

        const auto allocation_alignment = std::max(type.alignment, alignof(std::max_align_t));
        std::unique_ptr<void, ObjectDeleter> working(::operator new(type.size, std::align_val_t(allocation_alignment)),
                                                     ObjectDeleter{type, allocation_alignment});
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
