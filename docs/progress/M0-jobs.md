# M0 Jobs 任务系统报告

- 日期：2026-11-19
- 实现分支：`feature/m0-jobs`
- 状态：本阶段已完成并通过 Linux x64 本地验证；M0 整体尚未完成

## 阶段结论

本阶段建立了可运行的任务系统基础，完成从任务提交、并行拆分到等待完成的闭环。调度器支持任务内等待，
避免父任务等待子任务时占满全部工作线程；停止状态与条件变量使用同一锁域发布，避免收尾通知丢失。
M0 的后续工作可在此接口上增加任务依赖图，而不必先改动基础提交与等待语义。

## 已交付

- 新增 `StarJobs` 运行时模块，并建立对 `StarCore` 的公开依赖。
- 提供 `Jobs::Run()`，支持 `High`、`Normal`、`Background` 三档优先级。
- 提供按数量或随机访问范围调用的 `ParallelFor()`，按 grain 切分任务。
- 提供可复制、可移动的 `JobHandle`；`Wait()` 在等待期间会执行同一调度器中可运行的任务，支持任务内等待子任务。
- 使用工作线程本地任务队列和跨线程偷取，优先级队列优先执行高优先级任务。
- 提供 `Jobs::GetWorkerCount()` 和 `Jobs::IsWorkerThread()` 查询接口。
- 新增 `jobs-tests`，覆盖单任务、索引与范围并行、空批次、零 grain、嵌套等待和后台优先级。

## 接口约束

- 任务函数不得让异常逃逸；当前实现会在任务函数抛出未处理异常时终止进程。
- 异步范围版本不会复制容器，调用方必须保证范围在对应句柄完成前保持有效。
- 并行回调可能由多个线程同时调用，回调捕获的数据由调用方负责同步。
- 调度器启动时请求至少一个工作线程；等待线程也会参与执行队列中的任务。

## 本地验证

验证环境与 M0 基础设施报告一致。以下 Linux x64 预设均完成配置、构建和测试：

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure

cmake --preset linux-profile
cmake --build --preset linux-profile
ctest --preset linux-profile --output-on-failure

cmake --preset linux-shipping
cmake --build --preset linux-shipping
ctest --preset linux-shipping --output-on-failure
```

四个预设的 `hello-window-headless`、`core-tests`、`jobs-tests` 和 `platform-tests` 均通过。
`jobs-tests` 在 Development 配置下连续启动 500 次通过；ThreadSanitizer 构建连续运行 50 次通过。
`clang-format --dry-run --Werror` 和 `git diff --check` 通过。

Windows x64 与 macOS arm64 尚未执行本地验证。

## 尚未完成

- `TaskGraph` 及任务依赖声明与调度。
- Jolt 和 Box2D 的任务系统适配接口。
- 可配置的工作线程数量、线程命名和性能统计。
- 当前工作窃取队列使用互斥锁保护；无锁 Chase-Lev 队列仍作为后续性能优化项。

## 下一阶段

1. 增加 `TaskGraph` 依赖声明和执行调度，并补充依赖成功、失败传播和排空测试。
2. 抽象外部物理任务系统适配层，为 Jolt 和 Box2D 接入预留稳定接口。
3. 增加工作线程数量配置、线程命名与基础调度统计。
