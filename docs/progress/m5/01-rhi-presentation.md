# M5-01 RHI 呈现与绘制能力

- 日期：2026-11-28
- 分支：`feat/m5-rhi-presentation`
- 状态：Linux x64 通过；Windows x64、macOS arm64 未验证

## 背景

ADR-0019 的退出条件要求：M5 的 ImGui 后端接入前，把图形管线、采样器、描述符组、栅栏、纹理视图与交换链补进 RHI，或者另立 ADR 改为每个后端各有一套渲染器。本次选择补齐 RHI，编辑器视口与 ImGui 绘制都走 RHI；ADR-0019 的状态与后续收敛仍由维护者在 M5 收尾时确认。

## 本阶段交付

### 呈现

- `DeviceCreateInfo` 增加 `native_window`（SDL 窗口句柄）。带窗口创建设备时，实例启用 SDL 提供的 surface 扩展并创建 `VkSurfaceKHR`；`Capabilities::supportsPresentation` 报告该能力。
- `SwapchainHandle` 与 `createSwapchain` / `getSwapchainInfo` / `acquireNextImage` / `resizeSwapchain` / `present`。交换链图像以 `TextureHandle` 暴露，渲染通道与纹理屏障对它与普通纹理一视同仁。
- 交换链格式限定为 `Bgra8Unorm` 或 `Rgba8Unorm`（surface 不提供时返回 `Unsupported`），垂直同步关闭时优先 `MAILBOX`，其次 `IMMEDIATE`。
- 图像获取使用 fence 等待；`submit` 本身同步等待 GPU，因此 `present` 不再依赖信号量。`DeviceError::Outdated` 表示需要按窗口尺寸重建交换链。
- 窗口层新增 `Window::getNativeHandle()`，返回底层 SDL 窗口指针。

### 绘制

| 对象 | 接口 |
| --- | --- |
| 着色器 | `createShaderModule`（SPIR-V） |
| 采样器 | `createSampler`（过滤、寻址） |
| 纹理视图 | `createTextureView` |
| 绑定 | `createBindGroupLayout` / `createPipelineLayout`（推送常量）/ `createBindGroup`（UBO、SSBO、combined image sampler） |
| 管线 | `createGraphicsPipeline`（顶点布局、拓扑、剔除、混合、动态视口与裁剪、深度状态、动态渲染格式） |
| 命令 | `beginRenderPass` / `endRenderPass`（动态渲染，自动布局转换）、`setViewport` / `setScissor`、`bindGraphicsPipeline`、`pushConstants`、绑定顶点/索引缓冲、`draw` / `drawIndexed` |
| 传输 | `writeBuffer` / `readBuffer`、`copyBufferToTexture`、`copyTextureToBuffer` |

- 渲染通道开始时把颜色目标从 `Undefined` 转到颜色附件布局并清屏；结束时按 `present_after` 转到呈现布局或 `Sampled`，视口面板因此可以直接采样渲染结果。
- 删除接口按资源类型拆分。管线、采样器、视图、绑定组立即销毁；缓冲与纹理沿用帧边界回收，避免正在使用的资源被提前释放。
- 设备启用 `VkPhysicalDeviceVulkan11Features::shaderDrawParameters`：Slang 为 `SV_VertexID` 生成的 SPIR-V 声明了 `DrawParameters` 能力，之前没有启用该特性时验证层会报错。

### Null 后端

新接口在 Null 后端全部实现：资源句柄可创建、绑定组按布局校验资源是否存在、写读缓冲校验内存类型与范围、交换链与呈现返回 `Unsupported`。新的 RHI 测试在无 GPU 环境下覆盖这些校验。

### 样例

`samples/present-triangle`：SDL 窗口 + RHI 交换链 + 一条无顶点缓冲的三角形管线，可用 `--frames=N`、`--validation` 控制。着色器由 `slangc` 在构建期编译为 SPIR-V。

## 验证结果

环境：Linux x64、GCC 15、llvmpipe（Lavapipe ICD），启用 Khronos 验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建 | 通过 |
| CTest | 18/18 通过 |
| `rhi-tests`（Null 校验） | 通过：绑定组缺资源、管线缺着色器、交换链不可用、缓冲读写范围与内存类型 |
| `rhi-tests`（Vulkan） | 通过：4×4 纹理清屏后回读，颜色通道 64/128/191/255（容差 ±2）；采样纹理绑定组创建与销毁 |
| `vulkan-present-triangle` | 通过：1280×720 交换链，3 帧呈现，验证层与同步验证无报错 |
| `vulkan-scene-forward` | 通过：M4 渲染路径未回归 |
| `public-headers` | 通过：新增公共头独立可编译 |

```bash
cmake --preset linux-dev -DSTAR_SLANGC=$(command -v slangc) -DSTAR_TEST_VULKAN=ON
cmake --build build/linux-dev
VK_ICD_FILENAMES=<lavapipe icd> LD_LIBRARY_PATH=<lavapipe lib> \
    STAR_TEST_REQUIRE_VULKAN=1 ctest --test-dir build/linux-dev
```

## 阶段边界

- 提交仍然同步等待 GPU：没有多帧并行、时间线信号量的等待也没有进入 `submit`。编辑器可用，帧率优化留到后续。
- 渲染通道只有单个颜色附件，没有深度附件、多重采样与多队列同步；M4 的 `ForwardRenderer` 仍按 ADR-0019 的短期条款直连 Vulkan，尚未改为消费 RHI。
- 交换链格式不含 sRGB 变体；surface 只提供 sRGB 格式时创建失败，而不是静默降级。
- `RHI` 通过 `void*` 接收 SDL 窗口，尚未抽象成与平台无关的 surface 描述。
- Windows x64、macOS arm64 与真实 GPU 未复验；以上结果来自本地软件 Vulkan。
