#include "archive_value.hpp"

#include <ryml.hpp>

#include <utility>

namespace star::serialization {

namespace {

struct YamlParseFailure {};

std::string_view view(ryml::csubstr text) {
    return text.empty() ? std::string_view{} : std::string_view(text.str, text.len);
}

ValueResult readNode(ryml::ConstNodeRef node, unsigned depth) {
    const auto invalid = std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    if (depth > max_archive_depth || node.has_anchor() || node.is_ref() || node.has_key_tag() || node.has_val_tag()) {
        return invalid;
    }

    if (node.is_map()) {
        ArchiveValue::Mapping mapping;
        for (const auto child : node.children()) {
            if (!child.has_key() || child.key_is_null()) {
                return invalid;
            }
            auto value = readNode(child, depth + 1);
            if (!value) {
                return value;
            }
            if (!mapping.emplace(view(child.key()), std::move(*value)).second) {
                return invalid;
            }
        }
        return ArchiveValue{std::move(mapping)};
    }
    if (node.is_seq()) {
        ArchiveValue::Sequence sequence;
        for (const auto child : node.children()) {
            auto value = readNode(child, depth + 1);
            if (!value) {
                return value;
            }
            sequence.push_back(std::move(*value));
        }
        return ArchiveValue{std::move(sequence)};
    }
    if (!node.has_val()) {
        return invalid;
    }
    if (node.val_is_null() && !node.is_val_quoted()) {
        return ArchiveValue{};
    }
    const auto text = view(node.val());
    if (!node.is_val_quoted() && isPlainScalar(text)) {
        return ArchiveValue{PlainScalar{std::string(text)}};
    }
    return ArchiveValue{std::string(text)};
}

void appendQuoted(std::string &output, std::string_view text) {
    constexpr std::string_view hex = "0123456789abcdef";
    output.push_back('"');
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            output.push_back('\\');
            output.push_back(character);
        } else if (byte < 32 || byte == 127) {
            output.append("\\x");
            output.push_back(hex[byte >> 4]);
            output.push_back(hex[byte & 15]);
        } else {
            output.push_back(character);
        }
    }
    output.push_back('"');
}

void writeNode(std::string &output, const ArchiveValue &value) {
    if (const auto *plain = std::get_if<PlainScalar>(&value.value)) {
        output.append(plain->text);
    } else if (const auto *text = std::get_if<std::string>(&value.value)) {
        appendQuoted(output, *text);
    } else if (const auto *sequence = std::get_if<ArchiveValue::Sequence>(&value.value)) {
        output.push_back('[');
        bool first = true;
        for (const auto &entry : *sequence) {
            if (!std::exchange(first, false)) {
                output.append(", ");
            }
            writeNode(output, entry);
        }
        output.push_back(']');
    } else if (const auto *mapping = std::get_if<ArchiveValue::Mapping>(&value.value)) {
        output.push_back('{');
        bool first = true;
        for (const auto &[name, entry] : *mapping) {
            if (!std::exchange(first, false)) {
                output.append(", ");
            }
            appendQuoted(output, name);
            output.append(": ");
            writeNode(output, entry);
        }
        output.push_back('}');
    } else {
        output.append("null");
    }
}

} // namespace

std::expected<ArchiveDocument, ArchiveError> readYaml(std::string_view source) {
    try {
        std::string input(source);
        ryml::Callbacks callbacks;
        callbacks.set_error_parse(
            [](ryml::csubstr, const ryml::ErrorDataParse &, void *) { throw YamlParseFailure{}; });
        ryml::EventHandlerTree handler(callbacks);
        ryml::Parser parser(&handler);
        const auto tree = ryml::parse_in_place(&parser, ryml::substr(input.data(), input.size()));
        if (tree.empty()) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
        }
        auto root = tree.rootref();
        if (root.is_stream()) {
            if (root.num_children() != 1) {
                return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
            }
            root = root.first_child();
        }
        auto value = readNode(root, 0);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return parseDocument(std::move(*value));
    } catch (const YamlParseFailure &) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

std::expected<std::string, ArchiveError> writeYaml(const ArchiveDocument &document) {
    try {
        auto root = documentValue(document);
        if (document.type_name.empty() || document.version == 0 || !validateValue(root)) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
        }
        const auto &envelope = std::get<ArchiveValue::Mapping>(root.value);
        std::string output = "Type: ";
        writeNode(output, envelope.at("Type"));
        output.push_back('\n');
        if (const auto version = envelope.find("Version"); version != envelope.end()) {
            output.append("Version: ");
            writeNode(output, version->second);
            output.push_back('\n');
        }
        output.append("Properties:");
        if (document.fields.empty()) {
            output.append(" {}\n");
        } else {
            output.push_back('\n');
            for (const auto &[name, field] : document.fields) {
                output.append("  ");
                appendQuoted(output, name);
                output.append(": ");
                writeNode(output, field);
                output.push_back('\n');
            }
        }
        return output;
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

} // namespace star::serialization
