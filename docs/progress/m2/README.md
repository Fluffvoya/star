# M2 RHI 与 Vulkan 进度

| 阶段 | 报告 | 验证 |
| --- | --- | --- |
| M2-01 | [RHI 核心契约与 Null 后端](01-rhi-null-backend.md) | Linux x64 `linux-dev` |

M2-01 建立了上层渲染代码可以依赖的资源句柄、描述符、能力查询、命令列表和提交接口，并提供不依赖 GPU 的 Null 后端。Vulkan、VMA、验证层、调试命名、时间戳查询、Slang/SPIR-V 和渲染图属于后续阶段。
