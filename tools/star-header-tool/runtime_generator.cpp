#include "runtime_generator.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

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
    CXSourceLocation location;
    unsigned start;
};

struct Context {
    CXFile file;
    std::string_view source;
    std::vector<Marker> markers;
    std::vector<Declaration> &declarations;
    bool valid = true;
};

bool isInFile(CXSourceLocation location, CXFile file) {
    CXFile actual = nullptr;
    clang_getExpansionLocation(location, &actual, nullptr, nullptr, nullptr);
    return actual != nullptr && clang_File_isEqual(actual, file) != 0;
}

bool isStdType(CXType type, std::string_view name) {
    const auto decl = clang_getTypeDeclaration(type);
    if (takeString(clang_getCursorSpelling(decl)) != name) {
        return false;
    }
    auto scope = clang_getCursorSemanticParent(decl);
    while (clang_Cursor_isInlineNamespace(scope)) {
        scope = clang_getCursorSemanticParent(scope);
    }
    return takeString(clang_getCursorSpelling(scope)) == "std" &&
           clang_getCursorKind(clang_getCursorSemanticParent(scope)) == CXCursor_TranslationUnit;
}

CXType templateType(CXType type, unsigned index) {
    return clang_getCanonicalType(clang_Type_getTemplateArgumentAsType(type, index));
}

bool isString(CXType type) {
    if (!isStdType(type, "basic_string")) {
        return false;
    }
    const auto character = templateType(type, 0);
    const auto traits = templateType(type, 1);
    const auto allocator = templateType(type, 2);
    return (character.kind == CXType_Char_S || character.kind == CXType_Char_U) && isStdType(traits, "char_traits") &&
           clang_equalTypes(character, templateType(traits, 0)) && isStdType(allocator, "allocator") &&
           clang_equalTypes(character, templateType(allocator, 0));
}

bool isFilesystemPath(CXType type) {
    const auto decl = clang_getTypeDeclaration(type);
    if (takeString(clang_getCursorSpelling(decl)) != "path") {
        return false;
    }
    auto scope = clang_getCursorSemanticParent(decl);
    while (clang_Cursor_isInlineNamespace(scope)) {
        scope = clang_getCursorSemanticParent(scope);
    }
    if (takeString(clang_getCursorSpelling(scope)) != "filesystem") {
        return false;
    }
    scope = clang_getCursorSemanticParent(scope);
    while (clang_Cursor_isInlineNamespace(scope)) {
        scope = clang_getCursorSemanticParent(scope);
    }
    return takeString(clang_getCursorSpelling(scope)) == "std" &&
           clang_getCursorKind(clang_getCursorSemanticParent(scope)) == CXCursor_TranslationUnit;
}

// An empty name identifies a type handled directly by Reflection.
std::optional<std::string> propertyType(CXType type) {
    type = clang_getCanonicalType(type);
    if (clang_isConstQualifiedType(type) || clang_isVolatileQualifiedType(type)) {
        return std::nullopt;
    }
    switch (type.kind) {
    case CXType_Bool:
    case CXType_Char_U:
    case CXType_UChar:
    case CXType_UShort:
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_Char_S:
    case CXType_SChar:
    case CXType_Short:
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Float:
    case CXType_Double:
    case CXType_LongDouble:
        return std::string{};
    case CXType_Record:
    case CXType_Enum:
        break;
    default:
        return std::nullopt;
    }

    const auto name = qualifiedName(clang_getTypeDeclaration(type));
    if (isString(type) || isFilesystemPath(type) || name == "star::Guid" || name == "star::Name" ||
        name == "star::math::Vector2" || name == "star::math::Vector3" || name == "star::math::Quaternion") {
        return std::string{};
    }
    if (isStdType(type, "vector") || isStdType(type, "deque") || isStdType(type, "array")) {
        const auto element = templateType(type, 0);
        if (element.kind == CXType_Bool && isStdType(type, "vector")) {
            return std::nullopt;
        }
        return propertyType(element);
    }
    if (isStdType(type, "map") || isStdType(type, "unordered_map")) {
        return isString(templateType(type, 0)) ? propertyType(templateType(type, 1)) : std::nullopt;
    }
    if (name.empty() || clang_Type_getNumTemplateArguments(type) >= 0) {
        return std::nullopt;
    }
    return name;
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

std::string sourcePosition(CXSourceLocation location) {
    CXFile file = nullptr;
    unsigned line = 0;
    unsigned column = 0;
    clang_getExpansionLocation(location, &file, &line, &column, nullptr);
    return takeString(clang_getFileName(file)) + ':' + std::to_string(line) + ':' + std::to_string(column);
}

void diagnose(CXSourceLocation location, std::string_view message) {
    std::cerr << sourcePosition(location) << ": error: " << message << '\n';
}

void diagnose(CXCursor cursor, std::string_view message) { diagnose(clang_getCursorLocation(cursor), message); }

const Marker *markerAt(const Context &context, CXSourceLocation location) {
    if (!isInFile(location, context.file)) {
        return nullptr;
    }
    const auto start = offset(location);
    const auto marker = std::ranges::lower_bound(context.markers, start, {}, &Marker::start);
    return marker != context.markers.end() && marker->start == start ? &*marker : nullptr;
}

bool hasGeneratedBody(CXCursor cursor, const Context &context) {
    struct Body {
        const Context &context;
        bool found = false;
    } body{context};
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            auto &state = *static_cast<Body *>(data);
            if (clang_getCursorKind(child) == CXCursor_FriendDecl) {
                const auto *marker = markerAt(state.context, clang_getCursorLocation(child));
                if (marker != nullptr && marker->macro == "STAR_GENERATED_BODY") {
                    state.found = true;
                    return CXChildVisit_Break;
                }
            }
            return CXChildVisit_Continue;
        },
        &body);
    return body.found;
}

void collectType(CXCursor cursor, const Marker &marker, Context &context) {
    const auto cursor_kind = clang_getCursorKind(cursor);
    std::string type_kind;
    if (cursor_kind == CXCursor_ClassDecl && marker.macro == "STAR_CLASS") {
        type_kind = "Class";
    } else if (cursor_kind == CXCursor_StructDecl && marker.macro == "STAR_STRUCT") {
        type_kind = "Struct";
    } else if (cursor_kind == CXCursor_EnumDecl && marker.macro == "STAR_ENUM") {
        type_kind = "Enum";
    }
    if (type_kind.empty() || !clang_isCursorDefinition(cursor)) {
        diagnose(marker.location, marker.macro + " must precede a matching type definition");
        context.valid = false;
        return;
    }

    const std::string name = qualifiedName(cursor);
    if (name.empty()) {
        diagnose(cursor, "reflected types require a named namespace or global scope");
        context.valid = false;
        return;
    }
    const bool has_body = hasGeneratedBody(cursor, context);
    if (cursor_kind == CXCursor_ClassDecl && !has_body) {
        diagnose(cursor, "STAR_CLASS requires STAR_GENERATED_BODY()");
        context.valid = false;
    }

    Declaration declaration{name, std::move(type_kind), marker.metadata, {}, {}, {}};
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
            } else if (clang_isDeclaration(member_kind)) {
                auto metadata = memberMetadata(child, "star:property:");
                const bool function = !metadata.has_value();
                if (function) {
                    metadata = memberMetadata(child, "star:function:");
                }
                if (!metadata) {
                    return CXChildVisit_Continue;
                }
                if ((!function && member_kind != CXCursor_FieldDecl) ||
                    (function && member_kind != CXCursor_CXXMethod)) {
                    diagnose(child, function ? "STAR_FUNCTION requires a method"
                                             : "STAR_PROPERTY requires a non-static data member");
                    state.valid = false;
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
                const auto nested = function ? std::optional<std::string>{std::string{}} : propertyType(member_type);
                const auto spelled = takeString(clang_getTypeSpelling(clang_getCursorType(child)));
                if (!nested) {
                    diagnose(child, "unsupported reflected property type '" + spelled + "'");
                    state.valid = false;
                    return CXChildVisit_Continue;
                }
                state.declaration.members.push_back({member_name, std::move(*metadata), function, *nested, spelled,
                                                     sourcePosition(clang_getCursorLocation(child))});
            }
            return CXChildVisit_Continue;
        },
        &members);
    context.declarations.push_back(std::move(declaration));
}

void visitScope(CXCursor scope, Context &context) {
    struct Scope {
        Context &context;
        const Marker *pending = nullptr;
    } state{context};
    clang_visitChildren(
        scope,
        [](CXCursor child, CXCursor, CXClientData data) {
            auto &scope_state = *static_cast<Scope *>(data);
            auto &current = scope_state.context;
            const auto kind = clang_getCursorKind(child);
            const auto location = clang_getCursorLocation(child);
            if (!clang_isDeclaration(kind) || !isInFile(location, current.file)) {
                return CXChildVisit_Continue;
            }

            const auto *previous = std::exchange(scope_state.pending, nullptr);
            if (previous != nullptr) {
                collectType(child, *previous, current);
            }
            if (kind == CXCursor_StaticAssert) {
                if (const auto *marker = markerAt(current, location); marker != nullptr) {
                    scope_state.pending = marker;
                }
            } else if (kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec || kind == CXCursor_ClassDecl ||
                       kind == CXCursor_StructDecl) {
                visitScope(child, current);
            }
            return CXChildVisit_Continue;
        },
        &state);
    if (state.pending != nullptr) {
        diagnose(state.pending->location, state.pending->macro + " must precede a type definition in the same scope");
        context.valid = false;
    }
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
    const auto file = clang_getFile(unit, path.string().c_str());
    std::size_t size = 0;
    const char *source = file == nullptr ? nullptr : clang_getFileContents(unit, file, &size);
    if (source == nullptr) {
        std::cerr << path << ":1:1: error: unable to read reflection input\n";
        return false;
    }
    Context context{file, std::string_view(source, size), {}, declarations};
    const auto cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            if (clang_getCursorKind(child) != CXCursor_MacroExpansion ||
                !isInFile(clang_getCursorLocation(child), static_cast<Context *>(data)->file)) {
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
                state.markers.push_back({std::move(macro), std::string(invocation.substr(open + 1, close - open - 1)),
                                         clang_getCursorLocation(child), start});
            }
            return CXChildVisit_Continue;
        },
        &context);
    std::ranges::sort(context.markers, {}, &Marker::start);
    visitScope(cursor, context);
    return context.valid;
}

bool validateProperties(const std::vector<Declaration> &declarations) {
    std::set<std::string_view, std::less<>> names;
    for (const auto &declaration : declarations) {
        names.insert(declaration.name);
    }
    bool valid = true;
    for (const auto &declaration : declarations) {
        for (const auto &member : declaration.members) {
            if (!member.nested_type.empty() && !names.contains(member.nested_type)) {
                std::cerr << member.location << ": error: unsupported reflected property type '" << member.spelled_type
                          << "' (" << member.nested_type << " has no reflection metadata in this module)\n";
                valid = false;
            }
        }
    }
    return valid;
}

std::string generateAnnotationHeader(std::vector<Declaration> declarations) {
    std::ranges::sort(declarations, {}, &Declaration::name);
    std::ostringstream output;
    output << "#pragma once\n#include <array>\n#include <string_view>\n\nnamespace star::generated {\n\n";
    for (const auto &declaration : declarations) {
        auto kind = declaration.kind;
        kind.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(kind.front())));
        const auto separator = declaration.name.rfind("::");
        const auto name = separator == std::string::npos ? declaration.name : declaration.name.substr(separator + 2);
        const auto member_count = std::ranges::count(declaration.members, false, &Member::function);
        output << "inline constexpr std::string_view " << kind << '_' << name
               << "_annotation = " << literal("star:" + kind + ':' + declaration.metadata) << ";\n";
        if (member_count == 0) {
            output << "inline constexpr std::array<std::string_view, 0> " << kind << '_' << name
                   << "_properties{};\n\n";
            continue;
        }
        output << "inline constexpr std::string_view " << kind << '_' << name << "_properties[] = {";
        for (const auto &member : declaration.members) {
            if (!member.function) {
                output << literal(member.name) << ", ";
            }
        }
        output << "};\n\n";
    }
    output << "} // namespace star::generated\n";
    return output.str();
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
            const auto nested = typeExpression(member.nested_type);
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
    std::set<std::filesystem::path> resolved_paths;
    for (const auto &dependency : dependencies) {
        // Resolve symlinks before CMake normalizes parent components in system include paths.
        std::error_code error;
        auto resolved = std::filesystem::canonical(dependency, error);
        resolved_paths.insert(error ? dependency : std::move(resolved));
    }
    std::string result = escapeDependency(output.generic_string()) + ":";
    for (const auto &dependency : resolved_paths) {
        result += " " + escapeDependency(dependency.generic_string());
    }
    return result + "\n";
}

} // namespace star::header_tool
