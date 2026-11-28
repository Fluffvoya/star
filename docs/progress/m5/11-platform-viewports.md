# M5-11 分离窗口与多视口

- 分支：`feat/m5-platform-viewports`
- 验证环境：Linux x64、`linux-dev`、llvmpipe Vulkan。Windows x64、macOS arm64 未验证。

## 交付

- 支持全局鼠标定位的 SDL 视频驱动可以将浮动面板显示为独立系统窗口。ImGui 的 SDL3 平台后端处理窗口、触点和文本输入。主窗口和分离面板仍使用 Star RHI 绘制。
- 每个分离窗口在同一 Vulkan 设备上拥有独立 surface 与交换链。尺寸按像素计算，关闭时先回收交换链和 surface，再销毁 SDL 窗口。
- 支持 `--single-window` 显式使用主窗口内的停靠布局。当前 Dear ImGui SDL3 后端在 Wayland 下使用同一单窗口路径。

## 验证

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建及软件 Vulkan CTest | 19/19 通过 |
| X11 分离窗口 | 从脱离的 Statistics 布局启动。用 SDL 测试事件将分离窗口从 420 × 300 调整为 500 × 340，再关闭，窗口数由 2 变为 1。主窗口继续绘制至第 30 帧。 |
| Wayland | 加载项目启动场景并绘制 30 帧，使用主窗口内停靠 |
| Vulkan 同步验证 | X11 和 Wayland 验证运行均无验证层错误 |

本阶段在 Linux 的 X11 和 Wayland 路径分别验证多窗口与单窗口。D3D12、Metal 及对应编辑器测试依赖后续后端。
