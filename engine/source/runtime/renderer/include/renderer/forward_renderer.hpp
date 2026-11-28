#pragma once

#include "asset/material.hpp"
#include "asset/model_asset.hpp"
#include "renderer/export.hpp"
#include "renderer/render_scene.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace star::renderer {

enum class ForwardRenderErrorCode : std::uint8_t { Unsupported, InvalidInput, BackendFailure };

struct ForwardRenderError {
    ForwardRenderErrorCode code;
    std::string message;
};

struct ForwardMaterial {
    MaterialVariantKey variant;
    std::optional<std::array<float, 4>> base_color;
    std::optional<float> metallic;
    std::optional<float> roughness;
};

struct ForwardRenderOptions {
    std::filesystem::path shader_directory;
    bool front_view = false;
    bool enable_validation = false;
    // Camera used when the scene provides more than one. Falls back to front_view framing when absent.
    std::uint32_t camera_index = 0;
    // Optional per-part flags consumed during create().
    std::span<const std::uint8_t> shadow_casters{};
};

struct ForwardImage {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> rgba;
};

[[nodiscard]] STAR_RENDERER_API ForwardMaterial defaultForwardMaterial();

// A renderer owns one uploaded static scene and its offscreen render targets.
// Call its methods on the thread that created it.
class STAR_RENDERER_API ForwardRenderer final {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<ForwardRenderer>, ForwardRenderError>
    create(const StaticModelAsset &model, const ForwardRenderOptions &options,
           const ForwardMaterial &material = defaultForwardMaterial());

    // Uses the scene camera selected by options.camera_index, its directional and point lights.
    [[nodiscard]] static std::expected<std::unique_ptr<ForwardRenderer>, ForwardRenderError>
    create(const RenderScene &scene, const ForwardRenderOptions &options,
           const ForwardMaterial &material = defaultForwardMaterial());

    ~ForwardRenderer();
    ForwardRenderer(const ForwardRenderer &) = delete;
    ForwardRenderer &operator=(const ForwardRenderer &) = delete;

    // Replaces the view and shadow cascades without rebuilding geometry. Bounds stay unchanged.
    [[nodiscard]] std::expected<void, ForwardRenderError> setCamera(const RenderCamera &camera);
    // Replaces the lighting rig. A null sun keeps the built-in directional light.
    [[nodiscard]] std::expected<void, ForwardRenderError> setLights(std::span<const RenderPointLight> point_lights,
                                                                    const RenderDirectionalLight *sun);

    // Returns GPU duration in milliseconds when timestamp queries are available.
    [[nodiscard]] std::expected<double, ForwardRenderError> render();
    [[nodiscard]] std::expected<ForwardImage, ForwardRenderError> capture() const;
    [[nodiscard]] std::string_view getDeviceName() const noexcept;

  private:
    struct Impl;
    explicit ForwardRenderer(std::unique_ptr<Impl> implementation) noexcept;

    [[nodiscard]] static std::expected<std::unique_ptr<ForwardRenderer>, ForwardRenderError>
    createImpl(const StaticModelAsset &model, const ForwardRenderOptions &options, const ForwardMaterial &material,
               const RenderScene *scene);

    std::unique_ptr<Impl> m_impl;
};

} // namespace star::renderer
