# M0-16 Core 单生产者单消费者队列阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-spsc-queue`
- 范围：固定容量的无锁 FIFO 队列 `SPSCQueue<T>`

## 已交付

- 一个生产者通过 `tryPush()` 或 `tryEmplace()` 提交，另一个消费者通过 `tryPop()` 取出。满载时保留调用方的值，空队列返回空结果，零容量安全拒绝入队。
- 槽位在构造阶段分配，两个原子游标以 acquire/release 顺序发布对象的构造与销毁。元素须可无异常移动和析构。元素构造失败时队列保留该槽位供下次使用。
- `core/core.hpp` 导出队列接口。使用者须在析构队列前停止生产和消费。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 5 项 CTest。
- 新增用例覆盖环绕、满载、空队列、零容量、移动所有权、构造失败和 50,000 个元素的跨线程 FIFO 顺序。
- SPSC 用例通过 ThreadSanitizer，Development 的 `core-tests` 连续运行 100 次通过。`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

多生产者、多消费者队列仍是独立的 Core 工作。
