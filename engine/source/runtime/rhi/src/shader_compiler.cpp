#include "rhi/shader_compiler.hpp"

#include "core/guid.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace star::rhi {
namespace {

[[nodiscard]] std::string stageName(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return "vert";
    case ShaderStage::Fragment:
        return "frag";
    case ShaderStage::Compute:
        return "comp";
    }
    return {};
}

[[nodiscard]] std::string quoteArgument(std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (const char character : value) {
        if (character == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

[[nodiscard]] std::filesystem::path makeTemporaryPath(std::string_view suffix) {
    return std::filesystem::temp_directory_path() /
           ("star-shader-" + star::Guid::generate().toString() + std::string(suffix));
}

} // namespace

std::expected<SpirvModule, ShaderCompileError> compileGlslToSpirv(std::string_view source,
                                                                   const ShaderCompileOptions &options) {
    if (source.empty() || options.entryPoint.empty() || options.compiler.empty() ||
        (options.validate && options.validator.empty())) {
        return std::unexpected(ShaderCompileError::InvalidSource);
    }
    const std::string stage = stageName(options.stage);
    if (stage.empty()) {
        return std::unexpected(ShaderCompileError::InvalidSource);
    }

    std::filesystem::path sourcePath;
    std::filesystem::path outputPath;
    try {
        sourcePath = makeTemporaryPath("." + stage);
        outputPath = makeTemporaryPath(".spv");
        std::ofstream sourceFile(sourcePath, std::ios::binary);
        if (!sourceFile) {
            return std::unexpected(ShaderCompileError::FileIoFailed);
        }
        sourceFile.write(source.data(), static_cast<std::streamsize>(source.size()));
        sourceFile.close();

        const std::string command = quoteArgument(options.compiler) + " " + quoteArgument(sourcePath.string()) +
                                     " -o " + quoteArgument(outputPath.string()) + " -fshader-stage=" + stage +
                                     " -fentry-point=" + quoteArgument(options.entryPoint);
        const int compileResult = std::system(command.c_str());
        if (compileResult != 0) {
            std::error_code removeError;
            std::filesystem::remove(sourcePath, removeError);
            std::filesystem::remove(outputPath, removeError);
            return std::unexpected(compileResult == 127 ? ShaderCompileError::CompilerUnavailable
                                                         : ShaderCompileError::CompileFailed);
        }

        if (options.validate) {
            const std::string validationCommand = quoteArgument(options.validator) + " " + quoteArgument(outputPath.string());
            if (std::system(validationCommand.c_str()) != 0) {
                std::error_code removeError;
                std::filesystem::remove(sourcePath, removeError);
                std::filesystem::remove(outputPath, removeError);
                return std::unexpected(ShaderCompileError::ValidationFailed);
            }
        }

        std::ifstream outputFile(outputPath, std::ios::binary);
        if (!outputFile) {
            return std::unexpected(ShaderCompileError::FileIoFailed);
        }
        const std::vector<char> bytes((std::istreambuf_iterator<char>(outputFile)), std::istreambuf_iterator<char>());
        if (bytes.size() < sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0) {
            return std::unexpected(ShaderCompileError::InvalidSpirv);
        }
        SpirvModule module;
        module.words.resize(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(module.words.data(), bytes.data(), bytes.size());
        if (module.words.front() != 0x07230203U) {
            return std::unexpected(ShaderCompileError::InvalidSpirv);
        }
        std::error_code removeError;
        std::filesystem::remove(sourcePath, removeError);
        std::filesystem::remove(outputPath, removeError);
        return module;
    } catch (...) {
        std::error_code removeError;
        if (!sourcePath.empty()) {
            std::filesystem::remove(sourcePath, removeError);
        }
        if (!outputPath.empty()) {
            std::filesystem::remove(outputPath, removeError);
        }
        return std::unexpected(ShaderCompileError::FileIoFailed);
    }
}

} // namespace star::rhi
