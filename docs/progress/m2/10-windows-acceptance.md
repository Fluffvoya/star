# M2-10 Windows x64 验收阶段报告

- 日期：2026-11-26
- 分支：`codex/windows-milestone-validation`
- 状态：Vulkan RHI、Win32 呈现、同步验证与 RenderDoc 抓帧均通过

## 本阶段交付

- 探测和无窗口上下文独立加载 Vulkan，保持加载库存活到实例和设备释放。补齐 Vulkan 队列属性的数据布局，绑定资源先销毁资源再释放内存。
- vcpkg 为 Windows 和 Linux 启用 SDL3 的 Vulkan 特性，固定 Khronos Vulkan-Headers（Apache-2.0 / MIT）和 volk（MIT）到 `1.4.363.0`。CMake 按 volk 的构建号版本匹配实例函数表所需的接口。
- `STAR_TEST_WINDOWS_VULKAN` 注册两项 CTest，要求 Slang、`spirv-val`、Vulkan 验证层、可用的 Vulkan 设备和原生窗口。

## 验证结果

Windows x64（Build 26300）、MinGW-w64 GCC 16.2.0、CMake 4.4.2、Ninja 1.13.2、`win-dev`、vcpkg `x64-mingw-dynamic`。

| 验收项 | 结果 |
| --- | --- |
| 完整 CTest | 13/13 通过 |
| `windows-vulkan-rhi` | 真实 Vulkan 设备通过探测、队列与同步提交、缓冲区和纹理内存、RHI 屏障、时间戳及资源回收测试。Null 和 RenderGraph 测试同时通过。 |
| `windows-vulkan-present` | Intel Raptor Lake-S 核显（驱动 101.7085）用 SDL3 的 Windows 驱动呈现 12 帧，Vulkan 同步验证通过。 |
| 独立 Win32 运行 | 同一设备呈现 120 帧，取得绘制及复制 pass 的 GPU 时间戳，未出现验证错误。 |
| SPIR-V 与反射 | Slang 2026.18.2 从源文件生成顶点和片元 SPIR-V 1.6 以及反射 JSON，SPIRV-Tools v2026.4 按 Vulkan 1.3 校验两个模块。 |
| RenderDoc 1.46 | 抓取并回放第 2 帧。Vulkan 事件中核对了 `Textured Mesh`、`vkCmdDraw`、`Copy to Swapchain` 和 `vkQueuePresentKHR`，资源列表中确认了 `Mesh Vertices`、`Checkerboard`、`Mesh Color` 与 `Swapchain Image`。 |

严格模式下，分别指定不存在的 Vulkan 加载库以及隐藏 Khronos 验证层 1.4.363，`windows-vulkan-rhi` 均按预期失败。普通 CTest 仍支持无 Vulkan 设备场景。

## 复验

按 [Windows 构建与测试指南](../../windows-build.md)配置并运行 Vulkan 专项 CTest。要单独复验 120 帧呈现，在同一个 PowerShell 中运行以下命令。

```powershell
$env:VK_LAYER_VALIDATE_SYNC = '1'
& "$buildDir/bin/star-vulkan-mesh.exe" --validation --frames=120
```

在 RenderDoc 中启动构建目录下的 `bin/star-vulkan-mesh.exe`，传入 `--no-validation --frames=600`，抓取第 2 帧并核对上表的事件与资源。同步验证与抓帧分开运行。

## 阶段边界

本报告覆盖 Windows x64 MinGW 和 Intel Vulkan 驱动。独立的旧版 GLSL 烟测需要 `glslc`，本轮验证覆盖 Slang 路径。MSVC、其他 GPU 驱动及 macOS arm64 待验证。
