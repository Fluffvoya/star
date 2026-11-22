#pragma once

#include "rhi/export.hpp"
#include "rhi/rhi.hpp"

#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace star::rhi {

struct RenderTextureHandle final {
    std::uint32_t id = std::numeric_limits<std::uint32_t>::max();
    [[nodiscard]] bool isValid() const noexcept { return id != UINT32_MAX; }
    friend constexpr bool operator==(RenderTextureHandle, RenderTextureHandle) noexcept = default;
};

struct RenderPassHandle final {
    std::uint32_t id = std::numeric_limits<std::uint32_t>::max();
    [[nodiscard]] bool isValid() const noexcept { return id != UINT32_MAX; }
    friend constexpr bool operator==(RenderPassHandle, RenderPassHandle) noexcept = default;
};

enum class RenderPassType : std::uint8_t { Raster, Compute, Copy };

enum class RenderGraphError : std::uint8_t {
    EmptyName,
    InvalidResource,
    InvalidPass,
    CycleDetected,
    AllocationFailed,
};

struct RenderTextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Unorm;
};

struct RenderResourceUse {
    RenderTextureHandle resource;
    bool write = false;
};

struct RenderPassDesc {
    std::string_view name;
    RenderPassType type = RenderPassType::Raster;
    std::span<const RenderResourceUse> uses;
};

struct RenderBarrier {
    RenderTextureHandle resource;
    RenderPassHandle fromPass;
    RenderPassHandle toPass;
};

struct RenderResourceLifetime {
    RenderTextureHandle resource;
    std::uint32_t firstPass = 0;
    std::uint32_t lastPass = 0;
};

struct CompiledRenderGraph {
    std::vector<RenderPassHandle> passOrder;
    std::vector<RenderBarrier> barriers;
    std::vector<RenderResourceLifetime> lifetimes;
};

class STAR_RHI_API RenderGraph final {
  public:
    RenderGraph() = default;
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;
    RenderGraph(RenderGraph &&) noexcept = default;
    RenderGraph &operator=(RenderGraph &&) noexcept = default;
    ~RenderGraph() = default;

    [[nodiscard]] std::expected<RenderTextureHandle, RenderGraphError>
    createTexture(std::string_view name, const RenderTextureDesc &description);
    [[nodiscard]] std::expected<RenderPassHandle, RenderGraphError> addPass(const RenderPassDesc &description);
    [[nodiscard]] std::expected<CompiledRenderGraph, RenderGraphError>
    compile(std::span<const RenderTextureHandle> outputs = {}) const;
    void clear() noexcept;

  private:
    struct Texture {
        std::string name;
        RenderTextureDesc description;
    };
    struct Pass {
        std::string name;
        RenderPassType type;
        std::vector<RenderResourceUse> uses;
    };

    std::vector<Texture> m_textures;
    std::vector<Pass> m_passes;
};

} // namespace star::rhi
