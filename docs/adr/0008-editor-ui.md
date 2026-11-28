# ADR-0008：编辑器 UI

- 状态：已确认
- 日期：2026-11-20
- 关联：[Star Engine 实现方案](../architecture/implementation-plan.md) §2.1、§17；[ADR-0003](0003-graphics-backends.md)

## 背景

编辑器需要停靠面板、属性检视器、内容浏览器、控制台、视口和多窗口支持。编辑器 UI 迭代速度和跨 RHI 一致性优先于复杂的原生控件体系。

## 备选方案与后果

- Qt 或原生 UI。控件体系完整，但引入更大的平台和部署成本，并增加 RHI 集成分叉。
- Dear ImGui 官方各后端。接入快，但需要维护多个图形 API 后端，难以复用引擎 RHI 资源。
- Dear ImGui docking 加引擎自绘 RHI 后端。编辑器开发快，跨后端统一，但需要自维护顶点、纹理和多视口渲染桥接。

## 决定

使用 Dear ImGui docking 分支作为编辑器 UI。ImGui 绘制通过 Star RHI 后端完成，不使用官方 Vulkan、D3D、Metal 后端。编辑器支持停靠布局、多视口、视口纹理和引擎字体资源。

## 后果

EditorUI 依赖 EditorCore 和 RHI，不进入运行时游戏模块。多视口窗口由 Platform 创建，交换链和绘制资源由 RHI 管理。ImGui 版本和 docking 提交必须固定并记录许可证。

## 验收边界

M5 验证主框架、停靠布局、视口、层级、检视器、内容浏览器和控制台。至少在 Vulkan、D3D12 和 Metal 上验证同一编辑器 UI 路径。
