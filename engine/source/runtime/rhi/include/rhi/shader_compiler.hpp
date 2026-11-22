#pragma once

#include "rhi/export.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace star::rhi {

enum class ShaderStage : std::uint8_t { Vertex, Fragment, Compute };

enum class ShaderCompileError : std::uint8_t {
    InvalidSource,
    CompilerUnavailable,
    CompileFailed,
    ValidationFailed,
    FileIoFailed,
    InvalidSpirv,
};

struct ShaderCompileOptions {
    ShaderStage stage = ShaderStage::Vertex;
    std::string_view entryPoint = "main";
    std::string_view compiler = "glslc";
    std::string_view validator = "spirv-val";
    bool validate = true;
};

struct SpirvModule {
    std::vector<std::uint32_t> words;
};

[[nodiscard]] STAR_RHI_API std::expected<SpirvModule, ShaderCompileError>
compileGlslToSpirv(std::string_view source, const ShaderCompileOptions &options = {});

} // namespace star::rhi
