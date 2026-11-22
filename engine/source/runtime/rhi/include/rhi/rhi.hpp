#pragma once

#include "rhi/export.hpp"

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
};

struct DeviceCreateInfo {
    bool enableValidation = false;
    std::uint32_t framesInFlight = 3;
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

struct TextureBarrier {
    TextureHandle texture;
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

  protected:
    CommandList() = default;
};

class STAR_RHI_API Device {
  public:
    virtual ~Device() = default;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    [[nodiscard]] virtual const Capabilities &getCapabilities() const noexcept = 0;
    [[nodiscard]] virtual std::expected<BufferHandle, DeviceError> createBuffer(const BufferDesc &description) = 0;
    [[nodiscard]] virtual std::expected<TextureHandle, DeviceError> createTexture(const TextureDesc &description) = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(BufferHandle buffer) noexcept = 0;
    [[nodiscard]] virtual std::expected<void, DeviceError> destroy(TextureHandle texture) noexcept = 0;
    [[nodiscard]] virtual bool isAlive(BufferHandle buffer) const noexcept = 0;
    [[nodiscard]] virtual bool isAlive(TextureHandle texture) const noexcept = 0;

    [[nodiscard]] virtual std::expected<CommandList *, DeviceError> beginCommandList(QueueType queue) = 0;
    [[nodiscard]] virtual std::expected<SubmitToken, DeviceError>
    submit(QueueType queue, std::span<CommandList *const> commandLists, std::span<const SyncPoint> waits) = 0;

    virtual void beginFrame() noexcept = 0;
    virtual void endFrame() noexcept = 0;

  protected:
    Device() = default;
};

[[nodiscard]] STAR_RHI_API std::expected<std::unique_ptr<Device>, DeviceError>
createNullDevice(const DeviceCreateInfo &createInfo = {});

} // namespace star::rhi
