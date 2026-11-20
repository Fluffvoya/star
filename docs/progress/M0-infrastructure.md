# M0 基础设施第一阶段报告

- 日期：2026-11-19
- 实现分支：`feature/m0-infrastructure`
- 实现提交：`29f2aa6`
- 状态：第一阶段已交付；M0 整体尚未完成

## 已交付

### 构建与工程入口

- 建立 CMake 3.28+、Ninja 和 C++23 的统一构建入口。
- 建立 `star_add_module()`，统一模块库类型、公共/私有 include、生成导出头、依赖链接和编译选项。
- 提供 Windows x64、Linux x64、macOS arm64 的 `debug`、`dev`、`profile`、`shipping` 配置、构建和测试预设。
- Development 与 Debug 使用动态模块；Profile 与 Shipping 选择静态模块库。
- 建立 SDL3 系统包发现入口，并在依赖缺失时给出明确配置错误。
- 编译产物统一输出到 `<build>/bin/` 与 `<build>/lib/`，产物名使用 kebab-case。
- 建立 CTest 本地测试入口和 `star-hello-window` 示例目标。

### Core

- GUID：UUID v4 生成、32 位十六进制解析和格式化。
- Name：线程安全字符串驻留、32 位 ID 和整数比较。
- 日志：日志级别、命名类别、紧凑格式化和控制台输出。
- 断言：`STAR_ASSERT`、`STAR_VERIFY`、`STAR_FATAL` 的配置相关行为。

### Platform

- SDL3 平台初始化与关闭。
- 不暴露 SDL 类型的窗口封装。
- 关闭请求和窗口尺寸变化事件。
- 基于单调时钟的帧间隔测量。

### 示例与测试

- `star-hello-window` 可创建窗口并输出帧时间；支持 `--frames=N`。
- Core 测试覆盖 GUID 生成、解析、往返和失败输入，以及 Name 驻留和比较。
- Platform 测试覆盖 headless 窗口创建、属性读取和帧时钟。
- headless 示例测试运行三帧，验证平台、窗口和主循环入口。

### Jobs（后续增量）

- 新增任务系统、并行 for、任务优先级和等待期间执行任务的能力。
- 详细范围、接口约束和验证结果见 [M0 Jobs 任务系统报告](M0-jobs.md)。

### Core 句柄与专用分配器（后续增量）

- 新增稠密代数句柄容器，以及线性、栈式、定长对象池和带标签的追踪分配器。
- 详细范围、接口约束和验证结果见 [M0 Core 句柄与专用分配器阶段报告](M0-core-handles-memory.md)。

### Core CVar（后续增量）

- 新增类型化运行时变量、按名称查询和文本修改能力。
- 详细范围、接口约束和验证结果见 [M0 Core CVar 阶段报告](M0-core-cvars.md)。

### Core YAML 分层配置（后续增量）

- 支持分层配置文件、命令行覆盖及原子应用 `CVars` 映射。
- 源码依赖和验证结果见 [M0 Core YAML 分层配置阶段报告](M0-layered-yaml-config.md)。

### Jobs TaskGraph（后续增量）

- 支持任务依赖声明、拓扑调度、失败传播和运行后重复执行。
- 详细范围、接口约束和验证结果见 [M0 Jobs TaskGraph 阶段报告](M0-task-graph.md)。

### Core Delegate（后续增量）

- 支持单播委托、多播委托、解绑句柄和弱对象绑定。
- 详细范围和验证结果见 [M0 Core Delegate 阶段报告](M0-core-delegates.md)。

### Core 容器（后续增量）

- 新增 `FlatMap`、`FlatSet`、`HashMap` 和 `RingBuffer`。
- 详细范围和验证结果见 [M0 Core 容器阶段报告](M0-core-containers.md)。

### Core SmallVector（后续增量）

- 新增支持内联容量和堆扩容的 `SmallVector`。
- 详细范围和验证结果见 [M0 Core SmallVector 阶段报告](M0-core-small-vector.md)。

### Core 数学（后续增量）

- 新增向量、四元数、列主序矩阵和基础变换运算。
- 详细范围和验证结果见 [M0 Core 数学阶段报告](M0-core-math.md)。

### Core 全局分配器选择（后续增量）

- 新增统一系统分配入口、对齐分配和 `STAR_ALLOCATOR` 选择解析。
- 详细范围和限制见 [M0 Core 全局分配器选择阶段报告](M0-global-allocator.md)。

### Jobs 外部任务适配（后续增量）

- 新增供外部库提交单任务回调并等待完成的接口。
- 详细范围和限制见 [M0 Jobs 外部任务适配阶段报告](M0-external-tasks.md)。

## 本地验证

验证环境：

| 项 | 版本 |
|---|---|
| 操作系统 | Linux x64 |
| 编译器 | GCC 16.2.1 |
| CMake | 4.5.0 |
| Ninja | 1.13.2 |
| SDL3 | 3.4.20 |

以下预设均完成配置、构建和测试：

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

基础设施阶段验证时，每个预设的 `core-tests`、`platform-tests` 和 `hello-window-headless` 均通过。
Jobs 后续增量的 `jobs-tests` 已在相同 Linux 预设中通过。
`clang-format --dry-run --Werror` 与 `git diff --check` 通过。

## 尚未完成

- M0 的整体验收尚未完成；Windows x64 和 macOS arm64 仅声明了预设，尚未实机构建和运行。
- Core 的全局 `operator new/delete` 替换、mimalloc 接入、其余容器和数学扩展尚待实现。启动器尚待接入配置路径与命令行入口。
- 日志目前只输出到控制台，文件、编辑器面板、平台调试输出和后台写出尚未实现。
- Platform 尚未覆盖输入、动态库加载、文件监视、内存映射、崩溃处理和线程工具。
- `STAR_SHIPPING_DEPS` 目前只启用策略提示；vcpkg manifest、固定 baseline 和静态依赖 triplet 尚未落地。
- 当前依赖系统中未发现 glm，因此数学相关设施尚未提供。
- 公共头文件独立编译检查尚未纳入自动化测试。
- 测试入口使用仓库内的轻量测试支持，doctest 尚未接入。
- `clang-tidy` 未完成本地检查：当前 Clang 23 无法解析 GCC 16 生成的 C++23 module mapper 参数；这不是源码诊断通过。

## 下一步

1. 在接入 Jolt 和 Box2D 时实现各自的任务依赖、范围划分和工作线程索引约定。
2. 补齐 Core 的其余容器和全局分配设施，逐步覆盖 M0 清单。
3. 确定 vcpkg baseline 与静态依赖 triplet，使 Profile/Shipping 的依赖策略可复现。
4. 在 Windows x64 和 macOS arm64 实机执行本地预设、测试和窗口示例。
5. 将公共头文件独立编译检查纳入 CTest。
