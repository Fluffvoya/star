#pragma once

#include "renderer/forward_renderer.hpp"

#include <filesystem>

namespace star::sample {

[[nodiscard]] renderer::ForwardMaterial loadSampleMaterial(const std::filesystem::path &path);

} // namespace star::sample
