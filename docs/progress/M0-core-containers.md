# M0 Core 容器阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-core-containers`
- 范围：有序扁平容器、哈希映射和环形缓冲区

## 已交付

- `FlatMap<K, V>` 使用有序连续存储，支持查找、插入或赋值、删除和遍历。键唯一，遍历顺序稳定。
- `FlatSet<K>` 使用有序连续存储，支持成员查询、插入、删除和遍历。
- `HashMap<K, V>` 提供基于标准库开放寻址实现的项目别名，保留标准哈希映射接口。
- `RingBuffer<T>` 提供固定容量 FIFO 存储、无分配的入队和出队、首尾查看及清空。容量为零时安全拒绝入队。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增测试覆盖扁平容器的排序和唯一性、哈希映射访问、环形缓冲区的 FIFO 顺序、满载拒绝和空容量路径。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

继续实现带内联存储的 `SmallVector`，并补齐全局分配器选择、数学类型和平台扩展。
