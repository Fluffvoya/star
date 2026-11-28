#pragma once

#include "rhi/export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

namespace star::rhi {

struct BufferTag;
struct TextureTag;
struct CommandListTag;
struct ShaderTag;
struct SamplerTag;
struct TextureViewTag;
struct BindGroupLayoutTag;
struct PipelineLayoutTag;
struct BindGroupTag;
struct PipelineTag;
struct SwapchainTag;

template <typename Tag> class Handle final {
  public:
    constexpr Handle() noexcept = default;
    constexpr Handle(std::uint32_t index, std::uint32_t generation) noexcept
        : m_index(index), m_generation(generation) {}

    [[nodiscard]] constexpr bool isValid() const noexcept { return m_generation != 0; }
    [[nodiscard]] constexpr std::uint32_t getIndex() const noexcept { return m_index; }
    [[nodiscard]] constexpr std::uint32_t getGeneration() const noexcept { return m_generation; }

    friend constexpr bool operator==(Handle, Handle) noexcept = default;

  private:
    static constexpr std::uint32_t invalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t m_index = invalidIndex;
    std::uint32_t m_generation = 0;
};

using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using CommandListHandle = Handle<CommandListTag>;
using ShaderModuleHandle = Handle<ShaderTag>;
using SamplerHandle = Handle<SamplerTag>;
using TextureViewHandle = Handle<TextureViewTag>;
using BindGroupLayoutHandle = Handle<BindGroupLayoutTag>;
using PipelineLayoutHandle = Handle<PipelineLayoutTag>;
using BindGroupHandle = Handle<BindGroupTag>;
using GraphicsPipelineHandle = Handle<PipelineTag>;
using SwapchainHandle = Handle<SwapchainTag>;

enum class QueueType : std::uint8_t { Graphics, Compute, Copy };

enum class MemoryType : std::uint8_t { GpuOnly, Upload, Readback };

enum class BufferUsage : std::uint32_t {
    None = 0,
    Vertex = 1U << 0U,
    Index = 1U << 1U,
    Constant = 1U << 2U,
    Storage = 1U << 3U,
    Indirect = 1U << 4U,
    CopySource = 1U << 5U,
    CopyDestination = 1U << 6U,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr BufferUsage operator&(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}

enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1U << 0U,
    Storage = 1U << 1U,
    RenderTarget = 1U << 2U,
    DepthStencil = 1U << 3U,
    CopySource = 1U << 4U,
    CopyDestination = 1U << 5U,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

enum class TextureFormat : std::uint8_t { R8Unorm, Rgba8Unorm, Bgra8Unorm, D24UnormS8Uint };

enum class DeviceError : std::uint8_t {
    InvalidDescriptor,
    InvalidHandle,
    InvalidState,
    Unsupported,
    OutOfMemory,
    BackendUnavailable,
    BackendFailure,
    // The swapchain no longer matches the surface; recreate it and retry once.
    Outdated,
};

struct BufferDesc {
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryType memory = MemoryType::GpuOnly;
    std::string_view debugName;
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    TextureFormat format = TextureFormat::Rgba8Unorm;
    TextureUsage usage = TextureUsage::None;
    MemoryType memory = MemoryType::GpuOnly;
    std::string_view debugName;
};

struct Capabilities {
    bool supportsComputeQueue = false;
    bool supportsCopyQueue = false;
    bool supportsTimestampQueries = false;
    bool supportsDebugMarkers = false;
    bool supportsValidation = false;
    // True when the device owns a window surface and can create swapchains.
    bool supportsPresentation = false;
};

struct DeviceCreateInfo {
    bool enableValidation = false;
    std::uint32_t framesInFlight = 3;
    // SDL_Window* used to create the presentation surface. Devices without one render offscreen only.
    void *native_window = nullptr;
};

struct SubmitToken {
    std::uint64_t value = 0;
    friend constexpr bool operator==(SubmitToken, SubmitToken) noexcept = default;
};

struct SyncPoint {
    SubmitToken token;
};

struct BufferBarrier {
    BufferHandle buffer;
};

enum class TextureState : std::uint8_t {
    Undefined,
    Sampled,
    Storage,
    ColorAttachment,
    DepthAttachment,
    CopySource,
    CopyDestination,
};

struct TextureBarrier {
    TextureHandle texture;
    TextureState before = TextureState::Undefined;
    TextureState after = TextureState::Undefined;
};

enum class ShaderStage : std::uint8_t { Vertex, Fragment, Compute };

struct ShaderModuleDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::span<const std::uint32_t> spirv;
    std::string_view debugName;
};

enum class FilterMode : std::uint8_t { Nearest, Linear };
enum class AddressMode : std::uint8_t { Repeat, ClampToEdge, MirroredRepeat };

struct SamplerDesc {
    FilterMode min_filter = FilterMode::Linear;
    FilterMode mag_filter = FilterMode::Linear;
    AddressMode address_u = AddressMode::ClampToEdge;
    AddressMode address_v = AddressMode::ClampToEdge;
    AddressMode address_w = AddressMode::ClampToEdge;
    std::string_view debugName;
};

struct TextureViewDesc {
    TextureHandle texture;
    std::string_view debugName;
};

enum class BindingType : std::uint8_t { UniformBuffer, StorageBuffer, CombinedImageSampler };

struct BindGroupLayoutEntry {
    std::uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    ShaderStage visibility = ShaderStage::Fragment;
};

struct BindGroupLayoutDesc {
    std::span<const BindGroupLayoutEntry> entries;
    std::string_view debugName;
};

struct PipelineLayoutDesc {
    std::span<const BindGroupLayoutHandle> bind_groups;
    std::uint32_t push_constant_size = 0;
    std::string_view debugName;
};

struct BindGroupEntry {
    std::uint32_t binding = 0;
    TextureViewHandle texture_view;
    SamplerHandle sampler;
    BufferHandle buffer;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct BindGroupDesc {
    BindGroupLayoutHandle layout;
    std::span<const BindGroupEntry> entries;
    std::string_view debugName;
};

enum class VertexFormat : std::uint8_t { Float, Float2, Float3, Float4, UByte4Norm, UByte4 };

struct VertexAttribute {
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
};

struct VertexBufferLayout {
    std::uint32_t stride = 0;
    std::span<const VertexAttribute> attributes;
};

enum class PrimitiveTopology : std::uint8_t { TriangleList, TriangleStrip, LineList };
enum class CullMode : std::uint8_t { None, Front, Back };
enum class CompareOp : std::uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendMode : std::uint8_t { None, Alpha };

struct GraphicsPipelineDesc {
    PipelineLayoutHandle layout;
    ShaderModuleHandle vertex_shader;
    ShaderModuleHandle fragment_shader;
    std::span<const VertexBufferLayout> vertex_buffers;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cull = CullMode::None;
    bool depth_test = false;
    bool depth_write = false;
    CompareOp depth_compare = CompareOp::LessEqual;
    BlendMode blend = BlendMode::None;
    std::span<const TextureFormat> color_formats;
    bool has_depth = false;
    TextureFormat depth_format = TextureFormat::D24UnormS8Uint;
    std::string_view debugName;
};

struct RenderPassBeginInfo {
    TextureHandle color_target;
    // Ends the pass in the present layout instead of the sampled layout. Requires a swapchain image.
    bool present_after = false;
    std::array<float, 4> clear_color{0.0F, 0.0F, 0.0F, 1.0F};
    bool clear = true;
    std::string_view debugName;
};

struct SwapchainDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool vsync = true;
    std::string_view debugName;
};

struct SwapchainInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Bgra8Unorm;
    std::uint32_t image_count = 0;
};

class STAR_RHI_API CommandList {
  public:
    virtual ~CommandList() = default;
    CommandList(const CommandList &) = delete;
    CommandList &operator=(const CommandList &) = delete;

    virtual void barrier(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures) noexcept = 0;
    virtual void beginDebugMarker(std::string_view name) noexcept = 0;
    virtual void endDebugMarker() noexcept = 0;
    virtual void writeTimestamp(std::uint32_t queryIndex) noexcept = 0;
    [[nodiscard]] virtual std::expected<double, DeviceError> readTimestampNanoseconds(std::uint32_t) noexcept {
        return std::unexpected(DeviceError::Unsupported);
    }

    // Dynamic rendering pass. The color target is cleared on begin and transitioned on end.
    virtual void beginRenderPass(const RenderPassBeginInfo &description) noexcept = 0;
    virtual void endRenderPass() noexcept = 0;

    virtual void setViewport(float x, float y, float width, float height) noexcept = 0;
    virtual void setScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) noexcept = 0;

    // Bind groups are applied in the order of their layouts in the pipeline layout.
    virtual void bindGraphicsPipeline(GraphicsPipelineHandle pipeline,
                                      std::span<const BindGroupHandle> bind_groups) noexcept = 0;
    virtual void pushConstants(const void *data, std::uint32_t size) noexcept = 0;

    virtual void bindVertexBuffer(BufferHandle buffer, std::uint64_t offset = 0) noexcept = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, std::uint64_t offset = 0, bool index32 = true) noexcept = 0;
    virtual void draw(std::uint32_t vertex_count, std::uint32_t instance_count = 1, std::uint32_t first_vertex = 0,
                      std::uint32_t first_instance = 0) noexcept = 0;
    virtual void drawIndexed(std::uint32_t index_count, std::uint32_t instance_count = 1, std::uint32_t first_index = 0,
                             std::int32_t vertex_offset = 0, std::uint32_t first_instance = 0) noexcept = 0;

    // Copies mip 0 of an upload buffer into a texture and leaves the texture sampled.
    virtual void copyBufferToTexture(BufferHandle source, TextureHandle destination) noexcept = 0;
    // Copies mip 0 of a texture into a readback buffer with tightly packed texels.
    virtual void copyTextureToBuffer(TextureHandle source, BufferHandle destination) noexcept = 0;
    // Copies the first `size` bytes between buffers.
    virtual void copyBuffer(BufferHandle source, BufferHandle destination, std::uint64_t size) noexcept = 0;

  protected:
    CommandList() = default;
};

class STAR_RHI_API Device {
  public:
    virtual ~Device() = default;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    [[nodiscard]] virtual const Capabilities &getCapabilities() const noexcept = 0;
    // Human readable backend or adapter name for diagnostics.
    [[nodiscard]] virtual std::string_view getDeviceName() const noexcept = 0;
    [[nodiscard]] virtual std::expected<BufferHandle, DeviceError> createBuffer(const BufferDesc &description) = 0;
    [[nodiscard]] virtual std::expected<TextureHandle, DeviceError> createTexture(const TextureDesc &description) = 0;

    // Host-visible writes only. Buffers created without Upload memory return Unsupported.
    [[nodiscard]] virtual std::expected<void, DeviceError>
    writeBuffer(BufferHandle buffer, std::span<const std::byte> data, std::uint64_t offset = 0) = 0;
    // Reads back from a Readback buffer. Call after the copy has been submitted.
    [[nodiscard]] virtual std::expected<void, DeviceError> readBuffer(BufferHandle buffer, std::span<std::byte> data,
                                                                      std::uint64_t offset = 0) = 0;

    [[nodiscard]] virtual std::expected<ShaderModuleHandle, DeviceError>
    createShaderModule(const ShaderModuleDesc &description) = 0;
    [[nodiscard]] virtual std::expected<SamplerHandle, DeviceError> createSampler(const SamplerDesc &description) = 0;
    [[nodiscard]] virtual std::expected<TextureViewHandle, DeviceError>
    createTextureView(const TextureViewDesc &description) = 0;

    [[nodiscard]] virtual std::expected<BindGroupLayoutHandle, DeviceError>
    createBindGroupLayout(const BindGroupLayoutDesc &description) = 0;
    [[nodiscard]] virtual std::expected<PipelineLayoutHandle, DeviceError>
    createPipelineLayout(const PipelineLayoutDesc &description) = 0;
    [[nodiscard]] virtual std::expected<BindGroupHandle, DeviceError>
    createBindGroup(const BindGroupDesc &description) = 0;
    [[nodiscard]] virtual std::expected<GraphicsPipelineHandle, DeviceError>
    createGraphicsPipeline(const GraphicsPipelineDesc &description) = 0;

    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(BufferHandle buffer) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(TextureHandle texture) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(ShaderModuleHandle shader) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(SamplerHandle sampler) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(TextureViewHandle view) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(BindGroupLayoutHandle layout) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(PipelineLayoutHandle layout) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(BindGroupHandle group) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(GraphicsPipelineHandle pipeline) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(SwapchainHandle swapchain) noexcept = 0;
    [[nodiscard]] virtual bool isAlive(BufferHandle buffer) const noexcept = 0;
    [[nodiscard]] virtual bool isAlive(TextureHandle texture) const noexcept = 0;

    [[nodiscard]] virtual std::expected<SwapchainHandle, DeviceError>
    createSwapchain(const SwapchainDesc &description) = 0;
    [[nodiscard]] virtual std::expected<SwapchainInfo, DeviceError>
    getSwapchainInfo(SwapchainHandle swapchain) const = 0;
    // Returns the swapchain image to render into. Textures stay valid until the next resize.
    [[nodiscard]] virtual std::expected<TextureHandle, DeviceError> acquireNextImage(SwapchainHandle swapchain) = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError>
    resizeSwapchain(SwapchainHandle swapchain, std::uint32_t width, std::uint32_t height) = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> present(SwapchainHandle swapchain) = 0;

    [[nodiscard]] virtual std::expected<CommandList *, DeviceError> beginCommandList(QueueType queue) = 0;
    [[nodiscard]] virtual std::expected<SubmitToken, DeviceError>
    submit(QueueType queue, std::span<CommandList *const> commandLists, std::span<const SyncPoint> waits) = 0;

    virtual void beginFrame() noexcept = 0;
    virtual void endFrame() noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> waitIdle() noexcept = 0;

  protected:
    Device() = default;
};

[[nodiscard]] STAR_RHI_API std::expected<std::unique_ptr<Device>, DeviceError>
createNullDevice(const DeviceCreateInfo &createInfo = {});

} // namespace star::rhi
