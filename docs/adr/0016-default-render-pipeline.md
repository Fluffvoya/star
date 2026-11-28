# ADR-0016：3D 默认渲染管线

- 状态：已确认
- 日期：2026-11-20
- 关联：[Star Engine 实现方案](../architecture/implementation-plan.md) §12；[ADR-0003](0003-graphics-backends.md)、[ADR-0004](0004-shader-language.md)

## 背景

默认 3D 管线需要覆盖现代后端和 D3D11 Basic 路径，同时支持透明、MSAA、材质扩展和 Apple Silicon 的 TBDR 架构。延迟渲染的 G-Buffer 成本和透明处理限制不适合作为唯一默认路径。

## 备选方案与后果

- 延迟渲染。光照组织清晰，但依赖 G-Buffer 布局，透明和 MSAA 成本较高。
- 传统 Forward。透明和 MSAA 简单，但大量动态光源下光源循环成本高。
- Clustered Forward+。以深度和屏幕空间簇组织光源，兼顾透明、MSAA 和材质灵活性，需要建设簇光源和剔除基础设施。

## 决定

默认 3D 管线采用 Clustered Forward+。Modern 层级使用 GPU 剔除和间接绘制，Basic 层级使用 CPU 剔除和图形队列。管线包含深度预通道、光源分簇、阴影、PBR 前向着色、IBL、透明、粒子和后处理。

## 后果

Renderer 必须通过 RenderGraph 管理资源读写和屏障。不同能力层级允许降级，但必须保持可见结果和调试诊断。管线设置通过 CVar 和可伸缩性预设暴露。

## 验收边界

M4 验证深度、分簇光照、CSM、PBR、IBL 和色调映射。Modern 与 Basic 路径必须分别验证，不能用 Null 后端代替图形后端验收。
