# M0-24 Windows x64 验收阶段报告

- 日期：2026-11-26
- 实现分支：`codex/windows-milestone-validation`
- 状态：四个 Windows 预设通过 M0 验收

## 已交付

- 适配 MinGW 的 Core 环境变量测试，并修复 Jobs DLL 在进程退出时的清理问题。
- MinGW 使用静态 mimalloc 或系统分配器，避免 vcpkg 的 mimalloc DLL 启动断言。

## 验证

Windows x64（Build 26300）、MinGW-w64 GCC 16.2.0、CMake 4.4.2、Ninja 1.13.2、vcpkg。

| 预设 | vcpkg 目标 triplet | 分配器 | M0 CTest | Win32 窗口 |
| --- | --- | --- | --- | --- |
| `win-debug` | `x64-mingw-dynamic` | System | 6/6 通过 | 呈现 3 帧 |
| `win-dev` | `x64-mingw-dynamic` | System | 6/6 通过 | 呈现 3 帧 |
| `win-profile` | `x64-mingw-static` | System、mimalloc | 6/6 通过 | 呈现 3 帧 |
| `win-shipping` | `x64-mingw-static` | System、mimalloc | 6/6 通过 | 呈现 3 帧 |

四套配置均构建并运行 `hello-window-headless`、`core-tests`、`jobs-tests`、`platform-tests`、`public-headers` 和 `jobs-worker-count`。Platform 覆盖线程命名、窗口、键鼠事件注入、动态库和文件映射。窗口另以 `SDL_VIDEODRIVER=windows` 运行 `star-hello-window --frames=3`。

Debug 和 Development 使用共享 SDL3，使 Platform DLL 与输入测试共享 SDL 事件队列。将 SDL3 静态链接到这两者时，输入测试会因无法找到 Platform 创建的窗口而失败。Profile 和 Shipping 的 Core 测试覆盖静态 mimalloc。复现命令见 [Windows 构建与测试指南](../../windows-build.md)。

## 阶段边界

本报告覆盖本机 MinGW-w64，MSVC 和 macOS arm64 待对应工具链验证。
