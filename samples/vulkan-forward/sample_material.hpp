#pragma once

#include "asset/material.hpp"

#include <array>
#include <filesystem>
#include <optional>

namespace star::sample {

struct SampleMaterial {
    MaterialVariantKey variant;
    std::optional<std::array<float, 4>> base_color;
    std::optional<float> metallic;
    std::optional<float> roughness;
};

[[nodiscard]] SampleMaterial defaultSampleMaterial();
[[nodiscard]] SampleMaterial loadSampleMaterial(const std::filesystem::path &path);

} // namespace star::sample
