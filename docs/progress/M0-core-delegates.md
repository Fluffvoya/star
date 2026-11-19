# M0 Core Delegate 阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-core-delegates`
- 范围：单播委托、多播委托与弱对象绑定

## 已交付

- `Delegate<R(Args...)>` 支持绑定、替换、解除和无绑定调用。复制委托共享同一可变绑定状态，避免副本之间出现悬空回调。
- `bindMember()` 以 `std::weak_ptr` 捕获对象。对象销毁后回调自动跳过，返回值委托使用默认构造值。
- `MulticastDelegate<void(Args...)>` 支持添加、移除、清空和广播。广播先复制当前回调快照，再在锁外执行，回调可以安全地修改委托。
- `DelegateHandle` 用于多播解绑。注册表和绑定状态使用互斥保护，支持并发广播、解绑和新增。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增测试覆盖绑定替换、弱对象失效、共享状态、多播快照、广播期间解绑以及并发访问。
- ThreadSanitizer 构建的 `core-tests` 连续运行 50 次通过。公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

继续补齐 Core 的其余容器、数学类型和全局分配器选择。
