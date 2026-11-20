#include <clang-c/Index.h>

#include "generation_cache.hpp"
#include "runtime_generator.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using star::header_tool::CachedGeneration;
using star::header_tool::Dependencies;
using star::header_tool::fingerprint;
using star::header_tool::fingerprintArguments;
using star::header_tool::fingerprintFile;
using star::header_tool::GeneratedFiles;
using star::header_tool::writeIfChanged;

constexpr std::string_view tool_version = "star-header-tool-2";

struct DependencyState {
    std::set<std::filesystem::path> paths;
    Dependencies fingerprints;
    bool cacheable = true;
};

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
    bool verbose = false;
};

std::string cursorString(CXString value) {
    const char *text = clang_getCString(value);
    std::string result = text == nullptr ? std::string{} : std::string{text};
    clang_disposeString(value);
    return result;
}

std::vector<std::string> clangArguments(const Options &options) {
    std::vector<std::string> arguments{"-x", "c++", "-std=c++23", "-DSTAR_REFLECTION_PARSER"};
#ifdef STAR_CLANG_RESOURCE_DIR
    arguments.emplace_back("-resource-dir=" STAR_CLANG_RESOURCE_DIR);
#endif
    for (const auto &directory : options.include_directories) {
        arguments.push_back("-I" + directory.string());
    }
    arguments.insert(arguments.end(), options.clang_arguments.begin(), options.clang_arguments.end());
    return arguments;
}

std::filesystem::path absolutePath(const std::filesystem::path &path) {
    return std::filesystem::absolute(path).lexically_normal();
}

std::optional<std::uint64_t> generationKey(const Options &options, std::span<const std::string> arguments,
                                           const std::filesystem::path &executable) {
    std::vector<std::string> parts{std::string(tool_version),
                                   cursorString(clang_getClangVersion()),
                                   std::filesystem::current_path().generic_string(),
                                   options.module,
                                   std::to_string(options.owner),
                                   absolutePath(options.output).generic_string()};
    parts.push_back(options.source.empty() ? "" : absolutePath(options.source).generic_string());
    parts.push_back(std::to_string(options.inputs.size()));
    for (const auto &path : options.inputs) {
        parts.push_back(absolutePath(path).generic_string());
    }
    parts.push_back(std::to_string(arguments.size()));
    parts.insert(parts.end(), arguments.begin(), arguments.end());
    for (const auto &argument : arguments) {
        // These options can hide inputs from clang_getInclusions().
        if (argument.starts_with('@') || argument.starts_with("-include-pch") || argument.starts_with("-ivfsoverlay") ||
            argument.starts_with("-fmodule")) {
            return std::nullopt;
        }
    }
    for (const char *variable :
         {"PATH", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH", "OBJCPLUS_INCLUDE_PATH",
          "INCLUDE", "SDKROOT", "DEVELOPER_DIR", "MACOSX_DEPLOYMENT_TARGET", "VCToolsInstallDir", "WindowsSdkDir",
          "WindowsSDKVersion", "SOURCE_DATE_EPOCH"}) {
        const char *value = std::getenv(variable);
        parts.emplace_back(value == nullptr ? "" : value);
    }
    if (const auto binary = fingerprintFile(executable)) {
        parts.push_back(std::to_string(*binary));
    }
    return fingerprintArguments(parts);
}

void collectDependencies(CXTranslationUnit unit, const std::filesystem::path &input, DependencyState &state) {
    const auto input_path = absolutePath(input);
    state.paths.insert(input_path);
    struct Context {
        CXTranslationUnit unit;
        DependencyState &state;
    } context{unit, state};
    clang_getInclusions(
        unit,
        [](CXFile file, CXSourceLocation *, unsigned, CXClientData data) {
            auto &inclusion = *static_cast<Context *>(data);
            auto &dependencies = inclusion.state;
            const auto name = cursorString(clang_getFileName(file));
            if (name.empty()) {
                dependencies.cacheable = false;
                return;
            }
            const auto path = std::filesystem::absolute(name);
            dependencies.paths.insert(path);
            std::size_t size = 0;
            const char *bytes = clang_getFileContents(inclusion.unit, file, &size);
            if (bytes == nullptr) {
                dependencies.cacheable = false;
                return;
            }
            const auto hash = fingerprint(std::string_view(bytes, size));
            const auto [entry, inserted] = dependencies.fingerprints.try_emplace(path, hash);
            if (!inserted && entry->second != hash) {
                dependencies.cacheable = false;
            }
        },
        &context);
    state.cacheable = state.cacheable && state.fingerprints.contains(input_path);
}

bool parseFile(const std::filesystem::path &path, std::span<const std::string> argument_storage,
               std::vector<star::header_tool::Declaration> &declarations, DependencyState &dependencies) {
    CXIndex clang_index = clang_createIndex(0, 0);
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
    if (valid) {
        collectDependencies(unit, path, dependencies);
    }
    if (valid) {
        valid = star::header_tool::collectDeclarations(unit, path, declarations);
    }
    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(clang_index);
    return valid;
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
        if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--input") {
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
    std::set<std::filesystem::path> output_paths;
    for (const auto &path : {options.output, options.source, options.depfile, options.cache}) {
        if (!path.empty() && !output_paths.insert(absolutePath(path)).second) {
            return false;
        }
    }
    for (const auto &input : options.inputs) {
        if (output_paths.contains(absolutePath(input))) {
            return false;
        }
    }
    if (options.source.empty()) {
        return options.depfile.empty() && options.module.empty() && options.owner == 0;
    }
    return options.owner != 0 && !options.module.empty() && options.module.front() >= 'a' &&
           options.module.front() <= 'z' &&
           options.module.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos;
}

bool publish(const Options &options, const GeneratedFiles &files, const std::set<std::filesystem::path> &paths) {
    if (!writeIfChanged(options.output, files.header) ||
        (!options.source.empty() && !writeIfChanged(options.source, files.source)) ||
        (!options.depfile.empty() &&
         !writeIfChanged(options.depfile, star::header_tool::generateDepfile(options.source, paths)))) {
        std::cerr << "star-header-tool: error: unable to write generated output\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) try {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::cerr << "usage: star-header-tool --input <header> [--input <header> ...] --output <file> "
                     "[--cache <file>] [--source <file> --module <identifier> --owner <id> [--depfile <file>]] "
                     "[--include-dir <directory>] [--clang-arg <argument>] [--verbose]\n";
        return 2;
    }

    const auto arguments = clangArguments(options);
    const auto key = options.cache.empty() ? std::nullopt : generationKey(options, arguments, argv[0]);
    if (key) {
        if (const auto cached = star::header_tool::readCache(options.cache, *key)) {
            if (options.verbose) {
                std::cout << "StarHeaderTool: cache hit\n";
            }
            std::set<std::filesystem::path> paths;
            for (const auto &entry : cached->dependencies) {
                paths.insert(entry.first);
            }
            return publish(options, cached->files, paths) ? 0 : 1;
        }
    }

    const bool runtime = !options.source.empty();
    DependencyState dependencies;
    std::vector<star::header_tool::Declaration> declarations;
    for (const auto &input : options.inputs) {
        if (!parseFile(input, arguments, declarations, dependencies)) {
            return 1;
        }
    }

    if (!star::header_tool::validateProperties(declarations)) {
        return 1;
    }
    GeneratedFiles files;
    if (runtime) {
        files.header = star::header_tool::generateHeader(options.module);
        files.source = star::header_tool::generateSource(options.module, options.owner, options.output, options.inputs,
                                                         std::move(declarations));
    } else {
        files.header = star::header_tool::generateAnnotationHeader(std::move(declarations));
    }
    if (!publish(options, files, dependencies.paths)) {
        return 1;
    }
    if (key && dependencies.cacheable &&
        !star::header_tool::writeCache(options.cache, *key,
                                       CachedGeneration{std::move(dependencies.fingerprints), std::move(files)})) {
        std::cerr << options.cache << ": error: unable to write reflection cache\n";
        return 1;
    }
    return 0;
} catch (const std::exception &exception) {
    std::cerr << "star-header-tool: error: " << exception.what() << '\n';
    return 1;
}
