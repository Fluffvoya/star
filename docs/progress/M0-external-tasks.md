# M0 Jobs 外部任务适配阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-external-tasks`
- 范围：外部库单任务提交与完成等待

## 已交付

- `ExternalTasks::enqueue()` 接收回调和借用的上下文指针，复用现有 Jobs 调度器，默认使用高优先级。
- `ExternalTasks::finish()` 等待任务完成并释放句柄。等待期间调用线程可协助执行任务，支持任务内等待其他任务。
- 调度前的分配或提交失败时，回调在提交线程同步执行。调用方应将 `nullptr` 视为无需等待，并保证上下文在任务完成前有效。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增用例覆盖借用上下文、空回调和空句柄以及嵌套提交和等待。Development 的 `jobs-tests` 连续运行 100 次通过。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

Jolt 的任务依赖和屏障、Box2D 的范围拆分和工作线程索引由各自的接入层处理，随实际物理库集成验证。
