# ADR-0013：游戏 UI

- 状态：已确认
- 日期：2026-11-20
- 关联：[Star Engine 实现方案](../architecture/implementation-plan.md) §2.1、§15；[ADR-0003](0003-graphics-backends.md)、[ADR-0011](0011-asset-formats.md)

## 背景

游戏 UI 需要布局、样式、文字、输入事件、动画和本地化，并且需要作为运行时系统工作。编辑器 UI 的即时模式模型不适合游戏内长期保留的 UI 树和布局状态。

## 备选方案与后果

- 直接使用 Dear ImGui。接入快，但不适合复杂布局、样式表、本地化和运行时 UI 状态。
- 使用平台原生控件。平台差异和发布环境限制较大。
- 自研 retained-mode UI，复用 Yoga、FreeType、HarfBuzz 等库。需要建设完整运行时，但可控制数据模型和渲染集成。

## 决定

游戏 UI 使用自研 retained-mode UI。元素树、布局、样式、事件和动画由 UI 模块管理，布局使用 Yoga，字体使用 FreeType 和 HarfBuzz，文字光栅化采用适合运行时的缓存方案。UI 通过 Renderer/RHI 绘制。

## 后果

游戏 UI 与 EditorUI 分离。UI 资产需要可序列化、可热重载和可本地化。输入事件必须经过 Platform/Input 抽象，不能把 SDL 类型暴露到游戏 API。

## 验收边界

M8 验证布局、样式、文字、CJK、本地化、鼠标键盘事件、手柄导航和 HUD。编辑器可先支持文件预览与热重载，完整可视化编辑器属于后续阶段。
