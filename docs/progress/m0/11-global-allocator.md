# M0-11 Core 全局分配器选择阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-global-allocator`
- 范围：统一系统分配入口与运行时选择

## 已交付

- `GlobalAllocator` 提供统一的对齐分配和释放入口，默认使用系统分配器。
- `STAR_ALLOCATOR` 支持 `system` 和 `mimalloc` 两个值。当前构建未接入 mimalloc 时，选择 `mimalloc` 返回 `Unavailable`，未知值返回 `InvalidEnvironment`。
- 分配入口验证零大小和非二次幂对齐，系统分配路径使用 C 运行库分配函数，避免被全局入口递归调用。
- Core 公共入口导出 `GlobalAllocator`，后续第三方库和可执行文件可在同一接口上接入专用分配器。
- 单体构建中的全局 `operator new/delete` 已接入 `GlobalAllocator`，共享库构建保留宿主进程的分配入口。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增测试覆盖环境变量选择、未知值、未安装 mimalloc、对齐分配和失败输入。
- Core 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer。公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 依赖策略

`vcpkg.json` 和 `vcpkg-configuration.json` 固定了 Shipping 依赖基线。CMake 在发现 mimalloc 时接入对应目标，未发现时保留系统分配路径。
