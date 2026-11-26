# M0-20 Platform 线程亲和性阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-platform-thread-affinity`
- 范围：设置调用线程可运行的逻辑 CPU 集合

## 已交付

- `setCurrentThreadAffinity()` 接收逻辑 CPU 索引列表。Windows 使用 `SetThreadAffinityMask`，Linux 使用 `pthread_setaffinity_np`。
- 空列表和超出平台掩码范围的索引通过 `std::expected` 返回错误。macOS 当前返回 `operation_not_supported`，因为其公开线程亲和性接口使用 tag 而不是 CPU 掩码，不能直接表达同一契约。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成构建和 6 项 CTest。
- Platform 线程测试覆盖空列表错误，线程命名和其他 Platform 测试继续通过。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。Windows x64 与 macOS arm64 尚未实机验证。

## 后续工作

Jobs 工作线程可在平台线程命名和亲和性接口稳定后接入调度统计。macOS 亲和性需要单独设计 tag 语义。
