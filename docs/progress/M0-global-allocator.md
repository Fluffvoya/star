# M0 Core 全局分配器选择阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-global-allocator`
- 范围：统一系统分配入口与运行时选择

## 已交付

- `GlobalAllocator` 提供统一的对齐分配和释放入口，默认使用系统分配器。
- `STAR_ALLOCATOR` 支持 `system` 和 `mimalloc` 两个值。当前构建未接入 mimalloc 时，选择 `mimalloc` 返回 `Unavailable`，未知值返回 `InvalidEnvironment`。
- 分配入口验证零大小和非二次幂对齐，系统分配路径使用 C++ 对齐 `operator new/delete`。
- Core 公共入口导出 `GlobalAllocator`，后续第三方库和可执行文件可在同一接口上接入专用分配器。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增测试覆盖环境变量选择、未知值、未安装 mimalloc、对齐分配和失败输入。
- Core 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer。公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 尚未完成

本阶段没有在可执行文件中替换全局 `operator new/delete`，也没有引入 mimalloc 依赖。后续固定依赖和发布构建策略确定后，再接入单一进程级分配器以及第三方库分配回调。
