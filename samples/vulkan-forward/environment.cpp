#include "environment.hpp"

#include "core/math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace star::sample {
namespace {

using namespace math;

constexpr std::uint32_t width = 128;
constexpr std::uint32_t height = 64;
constexpr std::size_t mip_count = 7;
constexpr std::uint32_t sample_count = 128;

Vector3 environmentDirection(float u, float v) {
    const float phi = (2.0F * u - 1.0F) * pi;
    const float theta = v * pi;
    const float sin_theta = std::sin(theta);
    return {std::cos(phi) * sin_theta, std::cos(theta), std::sin(phi) * sin_theta};
}

Vector3 skyRadiance(Vector3 direction) {
    const float sky = std::max(0.0F, direction.y);
    const float ground = std::max(0.0F, -direction.y);
    const auto sky_color = Vector3{0.16F, 0.22F, 0.34F} + Vector3{0.15F, 0.27F, 0.45F} * sky;
    const auto ground_color = Vector3{0.09F, 0.05F, 0.025F} * ground;
    const float key = std::pow(std::max(0.0F, dot(direction, {-0.48F, 0.80F, 0.36F})), 48.0F);
    const float fill = std::pow(std::max(0.0F, dot(direction, {0.60F, 0.48F, 0.64F})), 18.0F);
    return sky_color * (1.0F - ground) + ground_color + Vector3{3.2F, 2.6F, 1.7F} * key +
           Vector3{0.48F, 0.75F, 1.2F} * fill;
}

std::array<float, 9> shBasis(Vector3 d) {
    return {0.282095F,
            0.488603F * d.y,
            0.488603F * d.z,
            0.488603F * d.x,
            1.092548F * d.x * d.y,
            1.092548F * d.y * d.z,
            0.315392F * (3.0F * d.z * d.z - 1.0F),
            1.092548F * d.x * d.z,
            0.546274F * (d.x * d.x - d.y * d.y)};
}

std::array<EnvironmentPixel, 9> integrateDiffuse() {
    std::array<Vector3, 9> coefficients{};
    const float azimuth = 2.0F * pi / width;
    for (std::uint32_t row = 0; row < height; ++row) {
        const float v = (static_cast<float>(row) + 0.5F) / height;
        const float solid_angle = azimuth * (std::cos(pi * static_cast<float>(row) / height) -
                                             std::cos(pi * static_cast<float>(row + 1) / height));
        for (std::uint32_t column = 0; column < width; ++column) {
            const auto direction = environmentDirection((static_cast<float>(column) + 0.5F) / width, v);
            const auto radiance = skyRadiance(direction);
            const auto basis = shBasis(direction);
            for (std::size_t coefficient = 0; coefficient < coefficients.size(); ++coefficient) {
                coefficients[coefficient] = coefficients[coefficient] + radiance * (basis[coefficient] * solid_angle);
            }
        }
    }
    // Cosine lobe convolution divided by pi for the Lambertian BRDF.
    constexpr std::array band_weight{1.0F, 2.0F / 3.0F, 2.0F / 3.0F, 2.0F / 3.0F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F};
    std::array<EnvironmentPixel, 9> result{};
    for (std::size_t coefficient = 0; coefficient < result.size(); ++coefficient) {
        const auto irradiance = coefficients[coefficient] * band_weight[coefficient];
        result[coefficient] = {irradiance.x, irradiance.y, irradiance.z, 0.0F};
    }
    return result;
}

float radicalInverse(std::uint32_t index) {
    float result = 0.0F;
    for (float weight = 0.5F; index != 0; weight *= 0.5F, index >>= 1) {
        result += static_cast<float>(index & 1U) * weight;
    }
    return result;
}

std::array<Vector3, sample_count> importanceSamples(float roughness) {
    std::array<Vector3, sample_count> samples{};
    const float alpha = roughness * roughness;
    for (std::uint32_t index = 0; index < sample_count; ++index) {
        const float phi = 2.0F * pi * static_cast<float>(index) / sample_count;
        const float u = radicalInverse(index);
        const float cos_theta = std::sqrt((1.0F - u) / (1.0F + (alpha * alpha - 1.0F) * u));
        const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - cos_theta * cos_theta));
        samples[index] = {sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
    }
    return samples;
}

Vector3 prefilter(Vector3 direction, const std::array<Vector3, sample_count> &samples) {
    const Vector3 axis = std::abs(direction.y) < 0.99F ? Vector3{0, 1, 0} : Vector3{0, 0, 1};
    const auto tangent = normalize(cross(axis, direction));
    const auto bitangent = cross(direction, tangent);
    Vector3 color{};
    float total = 0.0F;
    for (const auto &sample : samples) {
        const auto half_vector = tangent * sample.x + bitangent * sample.y + direction * sample.z;
        const auto light = half_vector * (2.0F * dot(direction, half_vector)) - direction;
        const float weight = std::max(0.0F, dot(direction, light));
        if (weight > 0) {
            color = color + skyRadiance(light) * weight;
            total += weight;
        }
    }
    return color / total;
}

} // namespace

EnvironmentLighting buildSampleEnvironment() {
    EnvironmentLighting result{};
    result.diffuse = integrateDiffuse();
    for (std::size_t mip = 0; mip < mip_count; ++mip) {
        const auto mip_width = width >> mip;
        const auto mip_height = height >> mip;
        result.mip_levels.push_back({mip_width, mip_height, result.specular.size()});
        const auto samples = importanceSamples(static_cast<float>(mip) / static_cast<float>(mip_count - 1));
        for (std::uint32_t y = 0; y < mip_height; ++y) {
            for (std::uint32_t x = 0; x < mip_width; ++x) {
                const auto direction =
                    environmentDirection((static_cast<float>(x) + 0.5F) / static_cast<float>(mip_width),
                                         (static_cast<float>(y) + 0.5F) / static_cast<float>(mip_height));
                const auto radiance = mip == 0 ? skyRadiance(direction) : prefilter(direction, samples);
                result.specular.push_back({radiance.x, radiance.y, radiance.z, 1.0F});
            }
        }
    }
    return result;
}

} // namespace star::sample
