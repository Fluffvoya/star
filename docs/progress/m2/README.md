# M2 RHI 与 Vulkan 进度

| 阶段 | 报告 | 验证 |
| --- | --- | --- |
| M2-01 | [RHI 核心契约与 Null 后端](01-rhi-null-backend.md) | Linux x64 `linux-dev` |
| M2-02 | [Vulkan 加载与实例探测](02-vulkan-probe.md) | Linux x64 `linux-dev` |
| M2-03 | [Vulkan 逻辑设备与队列启动](03-vulkan-device-bootstrap.md) | Linux x64 `linux-dev` |

M2-01 建立了上层渲染代码可以依赖的资源句柄、描述符、能力查询、命令列表和提交接口，并提供不依赖 GPU 的 Null 后端。M2-02 增加了不依赖 Vulkan 头文件的运行时加载和实例探测。M2-03 完成了无窗口 Vulkan 实例、物理设备、队列族选择和逻辑设备启动。VMA、真实资源命令、验证层消息、调试命名、时间戳、Slang/SPIR-V 和渲染图属于后续阶段。
