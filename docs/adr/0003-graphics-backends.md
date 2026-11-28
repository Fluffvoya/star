# ADR-0003：图形后端

- 状态：已确认
- 日期：2026-11-20
- 关联：[Star Engine 实现方案](../architecture/implementation-plan.md) §1.2、§11；[ADR-0016](0016-default-render-pipeline.md)

## 背景

引擎需要覆盖 Windows、Linux 和 macOS，同时保留现代显式图形 API 的资源模型和低端兼容能力。不同 API 在资源状态、队列、描述符和呈现方式上差异明显。

## 备选方案与后果

- 只支持 Vulkan。Windows/Linux 覆盖较好，但无法满足 macOS 正式支持和 Windows D3D11 兼容目标。
- 采用现成跨 API 渲染库。初期接入快，但难以控制显式资源模型、调试能力和跨后端能力分层。
- 自研统一 RHI，分别实现 Vulkan、D3D12、Metal 和 D3D11。维护成本高，但能统一引擎上层接口和能力查询。

## 决定

提供统一 RHI，正式后端为 Vulkan、D3D12、原生 Metal 和 D3D11。RHI 按 Vulkan、D3D12 和 Metal 的显式模型设计，D3D11 实现 Basic 能力层并负责模拟降级。

Windows 默认尝试 D3D12，失败后按 `d3d12 → vulkan → d3d11` 尝试未使用的后端。用户可显式选择后端。Null 后端仅用于服务器模式和无 GPU 测试，不能替代正式后端验收。

## 后果

RHI 上层不能暴露 API 专用类型。资源使用带代数的句柄，资源销毁延迟到 GPU 使用完成。每个后端必须报告实际能力和最终启用的后端，回退不能被记录为目标后端通过。

## 验收边界

M2 验证 Linux 和 Windows Vulkan。M7 验证 Windows D3D12、Linux Vulkan、macOS arm64 Metal。M10 验证 Windows D3D11 Basic 路径及 D3D12/Vulkan 不可用时的回退。
