#include "runtime_generator.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string_view>

namespace star::header_tool {

namespace {

std::string takeString(CXString string) {
    const char *value = clang_getCString(string);
    std::string result = value == nullptr ? std::string{} : std::string(value);
    clang_disposeString(string);
    return result;
}

std::string qualifiedName(CXCursor cursor) {
    std::string name;
    while (!clang_Cursor_isNull(cursor) && clang_getCursorKind(cursor) != CXCursor_TranslationUnit) {
        auto part = takeString(clang_getCursorSpelling(cursor));
        if (part.empty()) {
            return {};
        }
        name = name.empty() ? std::move(part) : part + "::" + name;
        cursor = clang_getCursorSemanticParent(cursor);
    }
    return name;
}

unsigned offset(CXSourceLocation location) {
    unsigned result = 0;
    clang_getExpansionLocation(location, nullptr, nullptr, nullptr, &result);
    return result;
}

struct Marker {
    std::string macro;
    std::string metadata;
    unsigned start;
    unsigned end;
};

struct Context {
    std::string source;
    std::vector<Marker> markers;
    std::vector<Declaration> &declarations;
    bool valid = true;
};

bool onlyTrivia(std::string_view text) {
    while (!text.empty()) {
        if (std::isspace(static_cast<unsigned char>(text.front())) != 0) {
            text.remove_prefix(1);
        } else if (text.starts_with("//")) {
            const auto end = text.find('\n');
            if (end == std::string_view::npos) {
                return true;
            }
            text.remove_prefix(end);
        } else if (text.starts_with("/*")) {
            const auto end = text.find("*/", 2);
            if (end == std::string_view::npos) {
                return false;
            }
            text.remove_prefix(end + 2);
        } else {
            return false;
        }
    }
    return true;
}

std::optional<std::string> memberMetadata(CXCursor cursor, std::string_view prefix) {
    struct Annotation {
        std::string_view prefix;
        std::optional<std::string> value;
    } annotation{prefix, {}};
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            auto &result = *static_cast<Annotation *>(data);
            if (clang_getCursorKind(child) == CXCursor_AnnotateAttr) {
                auto text = takeString(clang_getCursorSpelling(child));
                if (text.starts_with(result.prefix)) {
                    result.value = text.substr(result.prefix.size());
                    return CXChildVisit_Break;
                }
            }
            return CXChildVisit_Continue;
        },
        &annotation);
    return annotation.value;
}

void diagnose(CXCursor cursor, std::string_view message) {
    CXFile file = nullptr;
    unsigned line = 0;
    unsigned column = 0;
    clang_getExpansionLocation(clang_getCursorLocation(cursor), &file, &line, &column, nullptr);
    std::cerr << takeString(clang_getFileName(file)) << ':' << line << ':' << column << ": error: " << message << '\n';
}

CXChildVisitResult visitType(CXCursor cursor, CXCursor, CXClientData data) {
    auto &context = *static_cast<Context *>(data);
    if (!clang_Location_isFromMainFile(clang_getCursorLocation(cursor))) {
        return CXChildVisit_Continue;
    }
    const auto kind = clang_getCursorKind(cursor);
    if (kind == CXCursor_Namespace) {
        return CXChildVisit_Recurse;
    }
    std::string_view macro;
    std::string_view type_kind;
    switch (kind) {
    case CXCursor_ClassDecl:
        macro = "STAR_CLASS";
        type_kind = "Class";
        break;
    case CXCursor_StructDecl:
        macro = "STAR_STRUCT";
        type_kind = "Struct";
        break;
    case CXCursor_EnumDecl:
        macro = "STAR_ENUM";
        type_kind = "Enum";
        break;
    default:
        return CXChildVisit_Continue;
    }

    const auto range = clang_getCursorExtent(cursor);
    const auto start = offset(clang_getRangeStart(range));
    const auto end = offset(clang_getRangeEnd(range));
    const auto marker =
        std::ranges::find_if(context.markers.rbegin(), context.markers.rend(), [&](const Marker &entry) {
            return entry.macro == macro && entry.end <= start &&
                   onlyTrivia(std::string_view(context.source).substr(entry.end, start - entry.end));
        });
    if (marker == context.markers.rend() || !clang_isCursorDefinition(cursor)) {
        return CXChildVisit_Recurse;
    }
    const std::string name = qualifiedName(cursor);
    if (name.empty()) {
        diagnose(cursor, "reflected types require a named namespace or global scope");
        context.valid = false;
        return CXChildVisit_Continue;
    }
    const bool has_body = std::ranges::any_of(context.markers, [&](const Marker &entry) {
        return entry.macro == "STAR_GENERATED_BODY" && entry.start > start && entry.end < end;
    });
    if (kind == CXCursor_ClassDecl && !has_body) {
        diagnose(cursor, "STAR_CLASS requires STAR_GENERATED_BODY()");
        context.valid = false;
    }

    Declaration declaration{name, std::string(type_kind), marker->metadata, {}, {}, {}};
    struct MemberContext {
        Declaration &declaration;
        bool has_body;
        bool &valid;
    } members{declaration, has_body, context.valid};
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData child_data) {
            auto &state = *static_cast<MemberContext *>(child_data);
            const auto member_kind = clang_getCursorKind(child);
            const auto member_name = takeString(clang_getCursorSpelling(child));
            if (member_kind == CXCursor_EnumConstantDecl) {
                state.declaration.enumerators.push_back(member_name);
            } else if (member_kind == CXCursor_CXXBaseSpecifier) {
                const auto base =
                    qualifiedName(clang_getTypeDeclaration(clang_getCanonicalType(clang_getCursorType(child))));
                if (!state.declaration.base.empty() || clang_getCXXAccessSpecifier(child) != CX_CXXPublic) {
                    diagnose(child, "reflection generation supports a single public base");
                    state.valid = false;
                }
                state.declaration.base = base;
            } else if (member_kind == CXCursor_FieldDecl || member_kind == CXCursor_CXXMethod) {
                const bool function = member_kind == CXCursor_CXXMethod;
                auto metadata = memberMetadata(child, function ? "star:function:" : "star:property:");
                if (!metadata) {
                    return CXChildVisit_Continue;
                }
                if (!state.has_body && clang_getCXXAccessSpecifier(child) != CX_CXXPublic) {
                    diagnose(child, "non-public reflected members require STAR_GENERATED_BODY()");
                    state.valid = false;
                    return CXChildVisit_Continue;
                }
                if ((!function && clang_Cursor_isBitField(child)) ||
                    (function && (clang_Cursor_getNumArguments(child) != 0 ||
                                  clang_getCursorResultType(child).kind != CXType_Void ||
                                  clang_CXXMethod_isStatic(child) || clang_CXXMethod_isDeleted(child)))) {
                    diagnose(child, "expected an addressable field or a non-static void method with no arguments");
                    state.valid = false;
                    return CXChildVisit_Continue;
                }
                const auto member_type = clang_getCanonicalType(clang_getCursorType(child));
                if (!function &&
                    (clang_isConstQualifiedType(member_type) || clang_isVolatileQualifiedType(member_type) ||
                     member_type.kind == CXType_LValueReference || member_type.kind == CXType_RValueReference)) {
                    diagnose(child, "reflected fields require a mutable value type");
                    state.valid = false;
                    return CXChildVisit_Continue;
                }
                const auto member_decl = clang_getTypeDeclaration(member_type);
                const auto type_name = qualifiedName(member_decl);
                const auto template_name = takeString(clang_getCursorSpelling(member_decl));
                std::string element_type;
                if (type_name.starts_with("std::")) {
                    int index = -1;
                    if (template_name == "vector" || template_name == "array") {
                        index = 0;
                    } else if (template_name == "map" || template_name == "unordered_map") {
                        index = 1;
                    }
                    if (index >= 0) {
                        const auto element =
                            clang_Type_getTemplateArgumentAsType(member_type, static_cast<unsigned>(index));
                        element_type = qualifiedName(clang_getTypeDeclaration(clang_getCanonicalType(element)));
                    }
                }
                state.declaration.members.push_back(
                    {member_name, type_name, std::move(*metadata), function, std::move(element_type)});
            }
            return CXChildVisit_Continue;
        },
        &members);
    context.declarations.push_back(std::move(declaration));
    return CXChildVisit_Recurse;
}

std::string literal(std::string_view text) {
    std::string result = "\"";
    for (const char character : text) {
        switch (character) {
        case '"':
        case '\\':
            result.push_back('\\');
            result.push_back(character);
            break;
        case '\n':
            result.append("\\n");
            break;
        case '\r':
            result.append("\\r");
            break;
        case '\t':
            result.append("\\t");
            break;
        default:
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string option(std::string_view metadata, std::string_view key, std::string fallback) {
    bool quoted = false;
    unsigned depth = 0;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= metadata.size(); ++index) {
        const char character = index == metadata.size() ? ',' : metadata[index];
        if (quoted && character == '\\' && index + 1 < metadata.size()) {
            ++index;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (!quoted && character == '(') {
            ++depth;
        } else if (!quoted && character == ')' && depth != 0) {
            --depth;
        } else if (!quoted && depth == 0 && character == ',') {
            const auto entry = trim(metadata.substr(start, index - start));
            const auto separator = entry.find('=');
            if (trim(entry.substr(0, separator)) == key) {
                return separator == std::string_view::npos ? "true" : std::string(trim(entry.substr(separator + 1)));
            }
            start = index + 1;
        }
    }
    return fallback;
}

std::string stringOption(std::string_view metadata, std::string_view key) {
    const auto value = option(metadata, key, "\"\"");
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        return "\"\"";
    }
    // The metadata represents a C++ string literal from the annotation.
    return value;
}

std::string escapeDependency(std::string_view path) {
    std::string escaped;
    for (const char character : path) {
        if (character == ' ' || character == '#') {
            escaped.push_back('\\');
        } else if (character == '$') {
            escaped.push_back('$');
        }
        escaped.push_back(character);
    }
    return escaped;
}

} // namespace

bool collectDeclarations(CXTranslationUnit unit, const std::filesystem::path &path,
                         std::vector<Declaration> &declarations) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    Context context{std::string(std::istreambuf_iterator<char>(input), {}), {}, declarations};
    const auto cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            if (clang_getCursorKind(child) != CXCursor_MacroExpansion ||
                !clang_Location_isFromMainFile(clang_getCursorLocation(child))) {
                return CXChildVisit_Continue;
            }
            auto &state = *static_cast<Context *>(data);
            auto macro = takeString(clang_getCursorSpelling(child));
            if (macro != "STAR_CLASS" && macro != "STAR_STRUCT" && macro != "STAR_ENUM" &&
                macro != "STAR_GENERATED_BODY") {
                return CXChildVisit_Continue;
            }
            const auto extent = clang_getCursorExtent(child);
            const auto start = offset(clang_getRangeStart(extent));
            const auto end = offset(clang_getRangeEnd(extent));
            const auto invocation = std::string_view(state.source).substr(start, end - start);
            const auto open = invocation.find('(');
            const auto close = invocation.rfind(')');
            if (open != std::string_view::npos && close > open && close != std::string_view::npos) {
                state.markers.push_back(
                    {std::move(macro), std::string(invocation.substr(open + 1, close - open - 1)), start, end});
            }
            return CXChildVisit_Continue;
        },
        &context);
    std::ranges::sort(context.markers, {}, &Marker::start);
    clang_visitChildren(cursor, &visitType, &context);
    return context.valid;
}

std::string generateHeader(const std::string &module) {
    return "#pragma once\n\n#include \"reflection/reflection.hpp\"\n\nnamespace star::generated::" + module +
           " {\n\nstd::span<const reflect::TypeInfo *const> types();\n\n}\n";
}

std::string generateSource(const std::string &module, std::uint64_t owner, const std::filesystem::path &header,
                           const std::vector<std::filesystem::path> &inputs, std::vector<Declaration> declarations) {
    std::ranges::sort(declarations, {}, &Declaration::name);
    std::ostringstream output;
    output << "#include " << literal(header.filename().generic_string()) << "\n";
    for (const auto &input : inputs) {
        output << "#include " << literal(std::filesystem::absolute(input).generic_string()) << "\n";
    }
    output << "\n#include <array>\n\nnamespace star::reflect {\n";
    for (const auto &declaration : declarations) {
        output << "\ntemplate <> struct GeneratedType<::" << declaration.name
               << "> { static const TypeInfo &info(); };\n";
    }
    auto typeExpression = [&](std::string_view type) -> std::string {
        const auto found = std::ranges::lower_bound(declarations, type, {}, &Declaration::name);
        if (found == declarations.end() || found->name != type) {
            return "nullptr";
        }
        return "&GeneratedType<::" + found->name + ">::info()";
    };
    for (const auto &declaration : declarations) {
        const std::string &name = declaration.name;
        output << "\nconst TypeInfo &GeneratedType<::" << name << ">::info() {\n    using T = ::" << name << ";\n";
        const auto function_count = std::ranges::count(declaration.members, true, &Member::function);
        output << "    static const std::array<PropertyInfo, " << declaration.members.size() - function_count
               << "> properties{{\n";
        for (const auto &member : declaration.members) {
            if (member.function) {
                continue;
            }
            output << "        [] { auto property = makeProperty<T, decltype(T::" << member.name
                   << "), &T::" << member.name << ">(" << literal(member.name) << ", PropertyFlags::None";
            for (const std::string_view flag : {"Transient", "ReadOnly"}) {
                if (option(member.metadata, flag, "false") == "true") {
                    output << " | PropertyFlags::" << flag;
                }
            }
            const auto nested = typeExpression(member.element_type.empty() ? member.type : member.element_type);
            output << ", nullptr, " << stringOption(member.metadata, "FormerName")
                   << "); property.metadata = " << literal(member.metadata) << ";";
            if (nested != "nullptr") {
                output << " property.resolve_nested_type = []() -> const TypeInfo * { return " << nested << "; };";
            }
            output << " return property; }(),\n";
        }
        output << "    }};\n    static const std::array<FunctionInfo, " << function_count << "> functions{{\n";
        for (const auto &member : declaration.members) {
            if (member.function) {
                output << "        {" << literal(member.name) << ", " << literal(member.metadata)
                       << ", [](void *object) noexcept { if (object == nullptr) return false; "
                          "try { static_cast<T *>(object)->"
                       << member.name << "(); return true; } catch (...) { return false; } }},\n";
            }
        }
        output << "    }};\n    static const std::array<EnumEntry, " << declaration.enumerators.size()
               << "> enumerators{{\n";
        for (const auto &entry : declaration.enumerators) {
            output << "        {" << literal(entry) << ", static_cast<std::uint64_t>(T::" << entry << ")},\n";
        }
        std::string base_access = "BaseAccess{}";
        if (typeExpression(declaration.base) != "nullptr") {
            base_access = "makeBaseAccess<T, ::" + declaration.base + ">()";
        }
        output << "    }};\n    static const TypeInfo type{\n"
               << "        makeTypeId(" << literal(name) << "), " << literal(name) << ", TypeKind::" << declaration.kind
               << ", sizeof(T), alignof(T), properties, makeTypeOps<T>(), " << owner << "ull, "
               << option(declaration.metadata, "Version", "1") << ", " << typeExpression(declaration.base) << ", "
               << stringOption(declaration.metadata, "FormerName") << ", enumerators, "
               << (declaration.kind == "Enum" ? "makeEnumOps<T>()" : "EnumOps{}") << ", functions, "
               << literal(declaration.metadata) << ", " << base_access;
        if (option(declaration.metadata, "CustomSerialize", "false") == "true") {
            output << ", [](void *object, serialization::Archive &archive) { static_cast<T "
                      "*>(object)->serialize(archive); }";
        }
        output << "};\n    return type;\n}\n";
    }
    output << "\n}\n\nnamespace star::generated::" << module
           << " {\n\nstd::span<const reflect::TypeInfo *const> types() {\n"
           << "    static const std::array<const reflect::TypeInfo *, " << declarations.size() << "> entries{{\n";
    for (const auto &declaration : declarations) {
        output << "        &reflect::GeneratedType<::" << declaration.name << ">::info(),\n";
    }
    output << "    }};\n    return entries;\n}\n\n}\n";
    return output.str();
}

std::string generateDepfile(const std::filesystem::path &output, const std::set<std::filesystem::path> &dependencies) {
    std::string result = escapeDependency(output.generic_string()) + ":";
    for (const auto &dependency : dependencies) {
        result += " " + escapeDependency(dependency.generic_string());
    }
    return result + "\n";
}

} // namespace star::header_tool
