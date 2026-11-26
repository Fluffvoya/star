# M0-23 基础设施总体验收报告

- 日期：2026-11-20
- 实现分支：`task/m0-complete-20261120`
- 状态：Linux x64 与 Windows x64 本地验收完成，macOS arm64 待验收

## 本次阶段交付

- 固定 Profile/Shipping 的 vcpkg manifest 与 baseline，补齐 mimalloc 发现和单体构建全局分配入口。
- 为 Core 日志增加文件 sink、刷新和失败返回，保留线程安全控制台输出。
- 为窗口示例接入分层配置路径和 `--set=key=value` 命令行覆盖。
- 汇总此前已合并的 Core、Jobs、Platform、公共头检查和输入事件阶段。

## Linux x64 验收矩阵

| 预设 | 配置 | 构建 | CTest |
|---|---|---|---|
| `linux-debug` | 通过 | 通过 | 6/6 通过 |
| `linux-dev` | 通过 | 通过 | 6/6 通过 |
| `linux-profile` | 通过 | 通过 | 6/6 通过 |
| `linux-shipping` | 通过 | 通过 | 6/6 通过 |

四个预设均通过 `hello-window-headless`、Core、Jobs、Platform、公共头独立编译和工作线程配置测试。格式检查和 `git diff --check` 通过。配置文件路径和 headless 窗口示例也已单独运行验证。

## 跨平台验收

- Windows x64 的四个预设已完成 MinGW 实机复验，见 [Windows 验收报告](24-windows-acceptance.md)。
- macOS arm64 的本地构建、运行及窗口测试待对应主机执行。

## M0 后续边界

正式启动器默认配置路径、文件监视、崩溃转储、doctest 替换和 glm 适配属于后续增量，当前未将它们伪装成 M0 已验收功能。
