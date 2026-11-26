# M0-19 Platform 输入事件阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-platform-input-events`
- 范围：SDL3 键盘、鼠标移动、鼠标按钮和滚轮事件

## 已交付

- `WindowEventType` 新增键盘按下/释放、鼠标移动、鼠标按钮按下/释放和滚轮事件。
- `Window::pollEvents()` 仅转发属于当前窗口的输入事件。键盘事件携带 SDL 扫描码和修饰键，鼠标移动携带位置、相对位移和按钮状态，鼠标按钮携带按钮号、点击次数和位置，滚轮携带滚动量、方向和鼠标位置。
- 事件仍只在主线程轮询，返回的 span 在下一次 `pollEvents()` 或窗口销毁前有效。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成构建和 6 项 CTest。
- Platform 测试使用 SDL 事件队列注入键盘和鼠标移动事件，验证当前窗口过滤、事件顺序、扫描码、修饰键和相对位移。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。待验平台为 Windows x64 和 macOS arm64。

## 后续工作

手柄、文本输入和输入动作映射由后续 Input 模块实现。
