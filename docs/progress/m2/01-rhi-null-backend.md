# M2-01 RHI 核心契约与 Null 后端阶段报告

- 日期：2026-11-22
- 分支：`task/m2-phase-01-20261122`
- 状态：Linux x64 本地验证完成，M2 Vulkan 阶段尚未开始

## 本阶段交付

- 新增 `StarRHI` 运行时模块，公共 API 位于 `engine/source/runtime/rhi/include/rhi/rhi.hpp`。
- 建立带代数的 `BufferHandle`、`TextureHandle` 和命令列表基础句柄类型，旧句柄在资源回收后失效。
- 定义缓冲区、纹理、内存类型、队列、资源用途、纹理格式、设备能力和提交同步的公共描述类型。
- 定义 `Device` 与 `CommandList` 的第一版接口，包含资源创建、延迟销毁、命令列表录制、提交和帧边界。
- 新增 Null 后端。它支持图形、计算和复制队列，记录屏障、调试标记和时间戳调用，并通过帧边界回收延迟销毁资源。
- 将 RHI 公共头文件独立编译检查和 CTest 接入仓库测试入口。

## 验证结果

在 Linux x64 的 `linux-dev` 预设执行：

```text
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

CTest 共 11 项全部通过，包含 `rhi-tests`、`public-headers`、生成反射增量测试和已有 Core、Reflection、Serialization、Jobs、Platform 及无窗口示例测试。

RHI 专项测试覆盖无效描述符、能力查询、资源创建与回收、代数句柄失效、命令列表单次提交和重复提交错误。

## 阶段边界

本阶段没有实现 Vulkan 实例与设备选择、VMA 分配、交换链、真实 GPU 命令录制、验证层消息、调试对象命名和 GPU 时间戳结果。Null 后端只用于接口测试、无 GPU 测试和服务器模式，不能作为 Vulkan 验收结果。

Windows x64、macOS arm64 和实际 Vulkan GPU 尚未在本阶段验证。
