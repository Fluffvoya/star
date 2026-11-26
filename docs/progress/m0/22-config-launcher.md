# M0-22 配置启动入口阶段报告

- 日期：2026-11-20
- 实现分支：`task/m0-complete-20261120`
- 范围：示例启动器接入分层配置和命令行覆盖

## 已交付

- `star-hello-window` 支持 `--engine-config=PATH`、`--project-config=PATH`、`--platform-config=PATH` 和 `--user-config=PATH`。
- 启动器把所有命令行参数传给 `Config::load()`，因此可直接使用 `--set=key=value` 覆盖配置。
- 配置加载完成后以事务方式应用 `CVars`，加载或应用失败时在创建窗口前退出。
- 保留 `--frames=N` 作为窗口示例的运行参数，未知参数继续由配置层忽略。

## 验证

- Linux x64 Debug 预设重新配置、构建并通过 6 项 CTest。
- `hello-window-headless` 继续验证无窗口驱动环境下的启动和退出路径。
- `clang-format --dry-run --Werror` 和 `git diff --check` 通过。

## 后续工作

正式 `GameLauncher` 和项目模板建立后，将把同一配置入口接到默认引擎、项目、平台和用户路径。
