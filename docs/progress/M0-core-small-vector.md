# M0 Core SmallVector 阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-core-small-vector`
- 范围：带内联存储的动态数组

## 已交付

- `SmallVector<T, InlineCapacity>` 在容量范围内直接使用对象内存储，超过容量后迁移到堆存储。
- 支持复制、移动、预留容量、尾部构造与插入、尾部删除、清空、索引和边界检查。
- `InlineCapacity == 0` 作为合法类型处理，首次插入时自动建立堆存储。元素要求支持无异常移动和销毁，以保证迁移期间状态一致。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增测试覆盖内联到堆迁移、复制、移动、清空、零内联容量和越界访问。
- Core 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer。公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

继续补齐全局分配器选择、数学类型和平台扩展。
