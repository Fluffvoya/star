# M0-18 Jobs 工作线程配置阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-jobs-worker-count`
- 范围：调度器启动时的工作线程数量配置

## 已交付

- `STAR_JOBS_WORKERS` 在 Jobs 调度器第一次初始化时读取，使用无符号十进制值创建对应数量的工作线程。
- 环境变量未设置、为空、为零或包含非法字符时回退到硬件并发数减一，机器只有一个硬件线程时使用一个工作线程。
- 新增独立的 `jobs-worker-count` CTest 进程，使用 `STAR_JOBS_WORKERS=2` 验证配置在调度器启动前生效。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 6 项 CTest。
- `jobs-worker-count` 通过环境变量启动测试。既有 Jobs、Platform、Core、公共头和窗口测试继续通过。
- `clang-format --dry-run --Werror` 和 `git diff --check` 通过。待验平台为 Windows x64 和 macOS arm64。

## 后续工作

线程命名、亲和性和调度统计仍按 Jobs 与 Platform 的边界逐步接入。
