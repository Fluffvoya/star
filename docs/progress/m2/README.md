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

M2-01 建立了上层渲染代码可以依赖的资源句柄、描述符、能力查询、命令列表和提交接口，并提供不依赖 GPU 的 Null 后端。M2-02 增加了不依赖 Vulkan 头文件的运行时加载和实例探测。M2-03 完成了无窗口 Vulkan 实例、物理设备、队列族选择和逻辑设备启动。M2-04 完成了命令池、一次性命令缓冲和 fence 等待提交。M2-05 完成了原生 Vulkan 缓冲区、内存需求查询、内存类型选择、绑定和释放烟测。M2-06 完成了原生 Vulkan 纹理、内存需求查询、绑定和释放烟测。M2-07 增加了 GLSL 到 SPIR-V 的工具调用和 `spirv-val` 校验。VMA、正式资源句柄、验证层消息、调试命名、时间戳、Slang 编译器集成和渲染图属于后续阶段。
