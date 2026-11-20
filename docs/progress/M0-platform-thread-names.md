# M0 Platform 线程命名阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-platform-thread-names`
- 范围：设置调用线程的原生名称

## 已交付

- `setCurrentThreadName()` 可在工作线程内调用。Windows 将 UTF-8 转为 UTF-16 后调用 `SetThreadDescription`，Linux 和 macOS 调用各自的 `pthread_setname_np`。
- 空名称、嵌入空字符、超过平台限制的名称及系统调用失败通过 `std::expected` 返回错误。Linux 与 macOS 分别接受至多 15 和 63 字节的名称。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成构建和 5 项 CTest。
- 测试在线程内设置名称并通过原生接口读回，另外验证空名称、嵌入空字符和超长名称。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

线程亲和性与 Jobs 工作线程集成按各自的调用边界继续实现。
