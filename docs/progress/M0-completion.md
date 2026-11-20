# M0 基础设施总体验收报告

- 日期：2026-11-20
- 实现分支：`task/m0-complete-20261120`
- 状态：Linux x64 本地验收完成，跨平台实机验收待 Windows x64 和 macOS arm64 主机

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

## 未完成的外部验收

- 当前主机是 Linux，无法在本次工作区内执行 Windows x64 和 macOS arm64 的本地编译、运行及窗口测试。
- 远程仓库已接受任务分支推送。环境没有 `gh` 和 GitHub API token，无法自动创建或合并 Pull Request，需要在 GitHub 页面完成该分支的 PR 操作。

## M0 后续边界

正式启动器默认配置路径、文件监视、崩溃转储、doctest 替换和 glm 适配属于后续增量，当前未将它们伪装成 M0 已验收功能。
