# M2 RHI 与 Vulkan 进度

| 阶段 | 报告 | 验证 |
| --- | --- | --- |
| M2-01 | [RHI 核心契约与 Null 后端](01-rhi-null-backend.md) | Linux x64 `linux-dev` |
| M2-02 | [Vulkan 加载与实例探测](02-vulkan-probe.md) | Linux x64 `linux-dev` |
| M2-03 | [Vulkan 逻辑设备与队列启动](03-vulkan-device-bootstrap.md) | Linux x64 `linux-dev` |
| M2-04 | [Vulkan 命令池与同步提交](04-vulkan-command-submit.md) | Linux x64 `linux-dev` |
| M2-05 | [Vulkan 原生缓冲区内存烟测](05-vulkan-buffer-memory-smoke-test.md) | Linux x64 `linux-dev` |
| M2-06 | [Vulkan 原生纹理内存烟测](06-vulkan-texture-memory-smoke-test.md) | Linux x64 `linux-dev` |
| M2-07 | [SPIR-V 着色器工具链烟测](07-spirv-shader-toolchain.md) | Linux x64 `linux-dev` |
| M2-08 | [RenderGraph 依赖与屏障计算](08-render-graph-core.md) | Linux x64 `linux-dev` |
| M2-09 | [Vulkan 纹理网格与平台验收](09-vulkan-textured-mesh.md) | Linux x64 通过 |
| M2-10 | [Windows x64 验收](10-windows-acceptance.md) | MinGW Development，Vulkan 呈现、同步验证和 RenderDoc 抓帧通过 |

当前分支提供 Vulkan 设备、Slang 着色器构建及旋转纹理网格示例。M2 已完成 Linux x64 和 Windows x64 的 Vulkan 呈现、同步验证及 RenderDoc 抓帧。
