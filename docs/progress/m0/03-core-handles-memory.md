# M0-03 Core 句柄与专用分配器阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-core-handles-memory`
- 范围：Core 句柄容器与专用内存分配器

## 已交付

- `SlotMap<T>` 使用稠密存储和代数句柄。删除时将末尾元素移入空位，存活句柄继续有效。槽位回收时递增代数，达到上限时退役槽位。插入失败时保留原有句柄和可用槽位。
- `LinearAllocator` 从调用方提供的缓冲区中按对齐要求分配，支持整体重置。`StackAllocator` 在同一机制上提供标记和回滚，拒绝其他实例的标记及重置前的标记。
- `PoolAllocator<T>` 在调用方提供的固定槽位中构造和销毁对象，复用已释放的槽位，并在分配器销毁时清理存活对象。
- `TrackedAllocator` 可包装系统分配或自定义回调，按 `MemTag` 统计当前字节数、峰值字节数和存活分配次数，支持多线程读取与更新。

## 接口约定

- 句柄在所属 `SlotMap` 内解析。跨结构性修改保留句柄，访问对象时重新获取借用指针。元素需要支持无异常移动和销毁。
- 线性和栈式分配器由调用方管理缓冲区、对象生命周期和线程归属。回滚后应丢弃指向已回收区域的标记与指针。
- 对象池独占借入的槽位。调用方应在对象销毁后丢弃借用指针。追踪分配器的释放操作使用原分配的地址、大小、对齐和标签，并要求共享的上游回调支持并发。

## 验证

验证环境为 Linux x64、GCC 16.2.1、CMake 4.5.0 和 SDL3 3.4.20。`linux-debug`、`linux-dev`、
`linux-profile` 和 `linux-shipping` 均完成配置和构建，四个预设各有 4 项 CTest 通过。

- 新增 Core 用例覆盖稠密移动、失效句柄、槽位复用、构造失败、分配对齐与耗尽、标记回滚、对象析构和并发统计。
- 新增公共头和 `core/core.hpp` 分别通过独立语法编译。`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- `core-tests` 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer 检查。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

继续补齐 Core 的分层配置、CVar 和其余容器。全局分配器选择、第三方分配器接入及编辑器内存统计随对应阶段交付。
