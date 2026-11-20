#include <clang-c/Index.h>

#include "runtime_generator.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view tool_version = "star-header-tool-1";

struct Options {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::filesystem::path cache;
    std::vector<std::filesystem::path> include_directories;
    std::filesystem::path source;
    std::filesystem::path depfile;
    std::string module;
    std::uint64_t owner = 0;
    std::vector<std::string> clang_arguments;
};

struct ReflectedDeclaration {
    std::string kind;
    std::string name;
    std::string annotation;
    std::vector<std::string> properties;
};

struct ParseContext {
    std::vector<ReflectedDeclaration> &declarations;
    const std::map<std::string, std::string> &source_markers;
};

std::string cursorString(CXString value) {
    const char *text = clang_getCString(value);
    std::string result = text == nullptr ? std::string{} : std::string{text};
    clang_disposeString(value);
    return result;
}

bool readFile(const std::filesystem::path &path, std::string &contents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return file.good() || file.eof();
}

std::uint64_t hashBytes(std::uint64_t hash, std::string_view bytes) {
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hashInputs(const Options &options, const std::set<std::filesystem::path> &dependencies) {
    std::uint64_t hash = 14695981039346656037ull;
    hash = hashBytes(hash, tool_version);
    for (const auto &directory : options.include_directories) {
        hash = hashBytes(hash, directory.generic_string());
    }
    for (const auto &path : dependencies) {
        std::string contents;
        if (!readFile(path, contents)) {
            continue;
        }
        hash = hashBytes(hash, path.generic_string());
        hash = hashBytes(hash, contents);
    }
    std::ostringstream stream;
    stream << std::hex << hash;
    return stream.str();
}

void collectDependencies(const std::filesystem::path &path, std::set<std::filesystem::path> &dependencies) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    if (!dependencies.insert(absolute).second) {
        return;
    }
    std::string source;
    if (!readFile(absolute, source)) {
        return;
    }
    static const std::regex include_pattern(R"include(^\s*#\s*include\s*"([^"]+)")include");
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (!std::regex_search(line, match, include_pattern)) {
            continue;
        }
        const auto included = absolute.parent_path() / match[1].str();
        if (std::filesystem::exists(included)) {
            collectDependencies(included, dependencies);
        }
    }
}

std::string annotation(CXCursor cursor) {
    std::string result;
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
            if (clang_getCursorKind(child) == CXCursor_AnnotateAttr) {
                *static_cast<std::string *>(data) = cursorString(clang_getCursorDisplayName(child));
                return CXChildVisit_Break;
            }
            return CXChildVisit_Continue;
        },
        &result);
    return result;
}

CXChildVisitResult visitDeclaration(CXCursor cursor, CXCursor, CXClientData data) {
    auto &context = *static_cast<ParseContext *>(data);
    auto &declarations = context.declarations;
    const auto name = cursorString(clang_getCursorSpelling(cursor));
    auto declaration_annotation = annotation(cursor);
    if (declaration_annotation.empty()) {
        const auto marker = context.source_markers.find(name);
        if (marker != context.source_markers.end()) {
            declaration_annotation = marker->second;
        }
    }
    if (declaration_annotation.starts_with("star:class:") || declaration_annotation.starts_with("star:struct:") ||
        declaration_annotation.starts_with("star:enum:")) {
        const auto kind = declaration_annotation.substr(5, declaration_annotation.find(':', 5) - 5);
        declarations.push_back({kind, name, declaration_annotation, {}});
        auto &declaration = declarations.back();
        clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor, CXClientData child_data) {
                auto &properties = *static_cast<std::vector<std::string> *>(child_data);
                const auto child_annotation = annotation(child);
                if (child_annotation.starts_with("star:property:")) {
                    properties.push_back(cursorString(clang_getCursorSpelling(child)));
                }
                return CXChildVisit_Continue;
            },
            &declaration.properties);
    }
    return CXChildVisit_Recurse;
}

bool parseFile(const std::filesystem::path &path, const Options &options,
               std::vector<ReflectedDeclaration> &declarations,
               std::vector<star::header_tool::Declaration> &runtime_declarations,
               std::set<std::filesystem::path> &dependencies) {
    CXIndex clang_index = clang_createIndex(0, 0);
    std::vector<std::string> argument_storage{"-x", "c++", "-std=c++23", "-DSTAR_REFLECTION_PARSER"};
#ifdef STAR_CLANG_RESOURCE_DIR
    argument_storage.emplace_back("-resource-dir=" STAR_CLANG_RESOURCE_DIR);
#endif
    for (const auto &directory : options.include_directories) {
        argument_storage.push_back("-I" + directory.string());
    }
    argument_storage.insert(argument_storage.end(), options.clang_arguments.begin(), options.clang_arguments.end());
    std::vector<const char *> arguments;
    arguments.reserve(argument_storage.size());
    for (const auto &argument : argument_storage) {
        arguments.push_back(argument.c_str());
    }
    CXTranslationUnit unit = clang_parseTranslationUnit(
        clang_index, path.string().c_str(), arguments.data(), static_cast<int>(arguments.size()), nullptr, 0,
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_Incomplete |
            CXTranslationUnit_DetailedPreprocessingRecord);
    if (unit == nullptr) {
        clang_disposeIndex(clang_index);
        std::cerr << path << ":1:1: error: unable to parse translation unit\n";
        return false;
    }
    bool valid = true;
    const unsigned diagnostic_count = clang_getNumDiagnostics(unit);
    for (unsigned diagnostic_index = 0; diagnostic_index < diagnostic_count; ++diagnostic_index) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, diagnostic_index);
        if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error) {
            std::cerr << cursorString(clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions()))
                      << '\n';
            valid = false;
        }
        clang_disposeDiagnostic(diagnostic);
    }
    if (valid && !options.source.empty()) {
        valid = star::header_tool::collectDeclarations(unit, path, runtime_declarations);
        dependencies.insert(std::filesystem::absolute(path).lexically_normal());
        clang_getInclusions(
            unit,
            [](CXFile file, CXSourceLocation *, unsigned, CXClientData data) {
                auto &paths = *static_cast<std::set<std::filesystem::path> *>(data);
                const auto included_path = cursorString(clang_getFileName(file));
                if (!included_path.empty()) {
                    paths.insert(std::filesystem::absolute(included_path).lexically_normal());
                }
            },
            &dependencies);
    } else if (valid) {
        std::string source;
        std::map<std::string, std::string> source_markers;
        if (readFile(path, source)) {
            static const std::regex marker_pattern(
                R"marker(STAR_(CLASS|STRUCT|ENUM)\s*\(([^)]*)\)\s*(class|struct|enum)\s+([A-Za-z_][A-Za-z0-9_]*))marker");
            for (std::sregex_iterator iterator(source.begin(), source.end(), marker_pattern), end; iterator != end;
                 ++iterator) {
                const auto &match = *iterator;
                std::string kind = match[1].str();
                std::ranges::transform(kind, kind.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                source_markers.emplace(match[4].str(), "star:" + kind + ":" + match[2].str());
            }
        }
        ParseContext context{declarations, source_markers};
        clang_visitChildren(clang_getTranslationUnitCursor(unit), &visitDeclaration, &context);
    }
    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(clang_index);
    return valid;
}

std::string generate(const std::vector<ReflectedDeclaration> &declarations) {
    auto sorted = declarations;
    std::ranges::sort(sorted, [](const auto &left, const auto &right) { return left.name < right.name; });
    std::ostringstream output;
    output << "#pragma once\n#include <string_view>\n\nnamespace star::generated {\n\n";
    for (const auto &declaration : sorted) {
        auto escape = [](std::string_view value) {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value) {
                if (character == '\\' || character == '"') {
                    escaped.push_back('\\');
                }
                escaped.push_back(character);
            }
            return escaped;
        };
        output << "inline constexpr std::string_view " << declaration.kind << "_" << declaration.name
               << "_annotation = \"" << escape(declaration.annotation) << "\";\n";
        output << "inline constexpr std::string_view " << declaration.kind << "_" << declaration.name
               << "_properties[] = {";
        for (const auto &property : declaration.properties) {
            output << "\"" << escape(property) << "\", ";
        }
        output << "};\n\n";
    }
    output << "} // namespace star::generated\n";
    return output.str();
}

bool parseOptions(int argc, char **argv, Options &options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto nextValue = [&](std::filesystem::path &destination) {
            if (index + 1 >= argc) {
                return false;
            }
            destination = argv[++index];
            return true;
        };
        if (argument == "--input") {
            std::filesystem::path input;
            if (!nextValue(input)) {
                return false;
            }
            options.inputs.push_back(std::move(input));
        } else if (argument == "--output") {
            if (!nextValue(options.output)) {
                return false;
            }
        } else if (argument == "--cache") {
            if (!nextValue(options.cache)) {
                return false;
            }
        } else if (argument == "--source") {
            if (!nextValue(options.source)) {
                return false;
            }
        } else if (argument == "--depfile") {
            if (!nextValue(options.depfile)) {
                return false;
            }
        } else if (argument == "--module" && index + 1 < argc) {
            options.module = argv[++index];
        } else if (argument == "--owner" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            const auto result = std::from_chars(value.data(), value.data() + value.size(), options.owner);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                return false;
            }
        } else if (argument == "--clang-arg" && index + 1 < argc) {
            options.clang_arguments.emplace_back(argv[++index]);
        } else if (argument == "--include-dir") {
            std::filesystem::path directory;
            if (!nextValue(directory)) {
                return false;
            }
            options.include_directories.push_back(std::move(directory));
        } else {
            return false;
        }
    }
    if (options.inputs.empty() || options.output.empty()) {
        return false;
    }
    if (options.source.empty()) {
        return options.depfile.empty() && options.module.empty() && options.owner == 0;
    }
    return options.cache.empty() && options.output != options.source && options.output != options.depfile &&
           options.source != options.depfile && options.owner != 0 && !options.module.empty() &&
           options.module.front() >= 'a' && options.module.front() <= 'z' &&
           options.module.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos;
}

bool writeIfChanged(const std::filesystem::path &path, std::string_view contents) {
    std::string existing;
    if (readFile(path, existing) && existing == contents) {
        return true;
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    output.close();
    if (!output) {
        std::cerr << path << ": error: unable to write generated output\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) try {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::cerr << "usage: star-header-tool --input <header> [--input <header> ...] --output <file> "
                     "[--cache <file> | --source <file> --module <identifier> --owner <id> [--depfile <file>]] "
                     "[--include-dir <directory>] [--clang-arg <argument>]\n";
        return 2;
    }

    std::set<std::filesystem::path> dependencies;
    if (!options.source.empty()) {
        std::vector<ReflectedDeclaration> legacy;
        std::vector<star::header_tool::Declaration> declarations;
        for (const auto &input : options.inputs) {
            if (!parseFile(input, options, legacy, declarations, dependencies)) {
                return 1;
            }
        }
        const auto source = star::header_tool::generateSource(options.module, options.owner, options.output,
                                                              options.inputs, std::move(declarations));
        return writeIfChanged(options.output, star::header_tool::generateHeader(options.module)) &&
                       writeIfChanged(options.source, source) &&
                       (options.depfile.empty() || writeIfChanged(options.depfile, star::header_tool::generateDepfile(
                                                                                       options.source, dependencies)))
                   ? 0
                   : 1;
    }

    for (const auto &input : options.inputs) {
        collectDependencies(input, dependencies);
    }
    const std::string fingerprint = hashInputs(options, dependencies);
    if (!options.cache.empty()) {
        std::ifstream cache(options.cache);
        std::string cached;
        std::getline(cache, cached);
        if (cached == fingerprint && std::filesystem::exists(options.output)) {
            return 0;
        }
    }

    std::vector<ReflectedDeclaration> declarations;
    std::vector<star::header_tool::Declaration> runtime_declarations;
    for (const auto &input : options.inputs) {
        if (!parseFile(input, options, declarations, runtime_declarations, dependencies)) {
            return 1;
        }
    }
    const std::string generated = generate(declarations);
    if (!writeIfChanged(options.output, generated)) {
        return 1;
    }
    if (!options.cache.empty()) {
        if (!options.cache.parent_path().empty()) {
            std::filesystem::create_directories(options.cache.parent_path());
        }
        std::ofstream cache(options.cache, std::ios::binary | std::ios::trunc);
        cache << fingerprint << '\n';
    }
    return 0;
} catch (const std::exception &exception) {
    std::cerr << "star-header-tool: error: " << exception.what() << '\n';
    return 1;
}
