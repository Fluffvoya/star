# M5-02 ImGui 与编辑器壳

- 日期：2026-11-28
- 分支：`feat/m5-editor-shell`
- 状态：Linux x64 通过；Windows x64、macOS arm64 未验证

## 本阶段交付

### ImGui 依赖

- 新增子模块 `engine/source/third-party/imgui`，固定 `v1.91.8-docking`（`11b3a7c8ca23201294464c7f368614a9106af2a1`）。与 EnTT、rapidyaml 一样按源码引入，不使用系统包。
- `StarImGui` 静态库只编译核心文件（`imgui`、`draw`、`tables`、`widgets`、`demo`），不编译官方的 SDL/Vulkan 后端：输入由引擎平台层转发，渲染走引擎 RHI。

### 平台层

- `Window::getNativeHandle()` 暴露底层 SDL 窗口，供 RHI 创建 surface。
- 新增 `WindowEventType::TextInput`（`code` 为 Unicode 码点），用于输入框与中文输入。

### 日志

- `core/log.hpp` 新增 `log::addSink` / `log::removeSink`：写入线程在释放日志锁后回调，回调只拿借用字符串，不能再次写日志。编辑器的控制台面板用它收集引擎全部日志。

### EditorUI 模块

- `ImGuiLayer`：ImGui 上下文、字体图集上传、着色器与管线、逐帧顶点/索引/常量缓冲、按 `ImDrawCmd` 设置裁剪矩形并提交 `drawIndexed`。
  - 后端标志：`star-platform` + `star-rhi`，启用 `ImGuiConfigFlags_DockingEnable`。
  - 布局保存到 `Saved/Editor/Layout.ini`；找不到目录时退回不保存。
  - 字体：按常见路径探测 Noto Sans CJK / PingFang / 微软雅黑，加载简体中文常用字集；找不到时使用 ImGui 默认字体。
  - `render()` 可以针对同一帧录制多次，便于把界面同时画到交换链和离屏纹理。
- `EditorShell`：主菜单（File / View / Help）、默认停靠布局（下方控制台、右侧统计）、控制台面板（按级别过滤、清屏、自动滚动）与统计面板（帧时、FPS、视口尺寸、绘制调用、GPU 名称、ImGui 版本）。

### StarEditor

- `engine/source/programs/star-editor`：创建窗口（1600×900）、带 surface 的 Vulkan 设备、交换链，驱动“事件 → ImGui → 录制 → 提交 → 呈现”的循环，处理窗口缩放与 `Outdated`。
- 日志写入 `Saved/Logs/StarEditor.log`。
- 命令行：`--frames=N`、`--validation`、`--screenshot=<file.ppm>`。截图把同一帧额外渲染到离屏纹理并回读为 PPM，用于无显示器环境的界面核对。

### RHI

- `Device::getDeviceName()`，用于统计面板显示适配器。
- `CommandList::copyBuffer` 与对应测试，补齐缓冲区间拷贝（截图与后续资产上传都会用到）。

## 验证结果

环境：Linux x64、GCC、llvmpipe（Lavapipe ICD），启用 Khronos 验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建 | 通过（`star-editor` 与 ImGui 着色器） |
| CTest | 18/18 通过 |
| 编辑器运行 | 8 帧，最后一帧记录 6 次 ImGui 绘制调用 |
| 验证层 | 无报错；设备销毁前描述符池已释放 |
| 界面截图 | 菜单栏、Statistics（右上）、Console（下方）与停靠分隔线可见，文字清晰可读 |
| 窗口尺寸 | 由桌面环境平铺为 1261×1525，界面按实际帧缓冲尺寸布局 |

```bash
cmake --preset linux-dev -DSTAR_SLANGC=$(command -v slangc) -DSTAR_TEST_VULKAN=ON
cmake --build build/linux-dev
VK_ICD_FILENAMES=<lavapipe icd> LD_LIBRARY_PATH=<lavapipe lib> \
    ./build/linux-dev/bin/star-editor --frames=8 --validation --screenshot=/tmp/editor.ppm
```

## 实现记录

- 首版界面全黑：`ImDrawIdx` 默认是 16 位，而 `bindIndexBuffer` 按 32 位绑定，索引被成对解释成越界顶点，几何整体退化。后端改为按 `sizeof(ImDrawIdx)` 选择索引宽度后恢复正常。后续接入其它 ImGui 版本时需要保留这条约束。
- ImGui 1.91.8 使用静态字体图集与 `SetTexID`；1.92 起改为动态纹理更新，升级时需要同步改造后端。

## 阶段边界

- 只启用停靠，不启用多视口（`ImGuiConfigFlags_ViewportsEnable`）：多视口需要为每个平台窗口创建独立交换链。
- 中央区域留空，视口面板在 M5-04 提供；当前没有项目加载与场景文档（M5-03）。
- 界面标签使用英文；CJK 字体只保证系统字体存在时可用，未随仓库分发字体资产。
- 输入映射覆盖鼠标、滚轮、常用按键与文本输入，没有手柄导航与 IME 候选窗定位；按键映射只覆盖字母、数字、功能键与导航键。
- Windows x64 与 macOS arm64 未验证；结果来自本地软件 Vulkan 与 X11/Wayland 桌面。
