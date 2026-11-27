#include "sample_material.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace star::sample {
namespace {

float readFactor(const serialization::ArchiveValue &value, const std::string &field) {
    const auto *scalar = std::get_if<serialization::PlainScalar>(&value.value);
    if (scalar) {
        const auto &text = scalar->text;
        float factor = 0.0F;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), factor);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(factor) &&
            factor >= 0.0F && factor <= 1.0F) {
            return factor;
        }
    }
    throw std::runtime_error("Expected a factor between 0 and 1 in " + field);
}

} // namespace

renderer::ForwardMaterial loadSampleMaterial(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream yaml;
    if (!stream || !(yaml << stream.rdbuf())) {
        throw std::runtime_error("Cannot read material " + path.string());
    }
    const auto document = serialization::readYaml(yaml.str());
    if (!document) {
        throw std::runtime_error("Cannot parse material field " + document.error().path);
    }
    const auto asset = readMaterial(*document, serialization::LoadPolicy::Strict);
    if (!asset) {
        throw std::runtime_error("Invalid material field " + asset.error().path);
    }

    renderer::ForwardMaterial material{};
    material.variant = asset->getVariantKey();
    if (material.variant.shader != renderer::defaultForwardMaterial().variant.shader) {
        throw std::runtime_error("The material shader GUID is not registered by the sample");
    }
    if (material.variant.state.blend != MaterialBlendMode::Opaque || !material.variant.state.depth_test) {
        throw std::runtime_error("The sample requires opaque materials with depth testing");
    }
    for (const auto &[field, value] : asset->parameters) {
        if (field == "Metallic") {
            material.metallic = readFactor(value, field);
        } else if (field == "Roughness") {
            material.roughness = readFactor(value, field);
        } else if (field == "BaseColor") {
            const auto *rgba = std::get_if<serialization::ArchiveValue::Sequence>(&value.value);
            if (!rgba || rgba->size() != 4) {
                throw std::runtime_error("BaseColor requires four factors");
            }
            auto &color = material.base_color.emplace();
            for (std::size_t channel = 0; channel < color.size(); ++channel) {
                color[channel] = readFactor((*rgba)[channel], field);
            }
        } else {
            throw std::runtime_error("Unknown sample material parameter " + field);
        }
    }
    return material;
}

} // namespace star::sample
