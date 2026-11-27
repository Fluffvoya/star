#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace star::renderer {

using EnvironmentPixel = std::array<float, 4>;

struct EnvironmentMip {
    std::uint32_t width;
    std::uint32_t height;
    std::size_t first_pixel;
};

struct EnvironmentLighting {
    std::array<EnvironmentPixel, 9> diffuse;
    std::vector<EnvironmentPixel> specular;
    std::vector<EnvironmentMip> mip_levels;
};

[[nodiscard]] EnvironmentLighting buildBuiltinEnvironment();

} // namespace star::renderer
