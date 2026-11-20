# M0 Platform 动态库阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-platform-dynamic-library`
- 范围：动态库加载、函数符号查找和句柄生命周期

## 已交付

- `DynamicLibrary` 以可移动对象持有一个动态库引用，析构时释放。调用方须在释放前停止使用已借出的函数指针。
- Windows 使用宽字符路径调用 `LoadLibraryExW` 和 `GetProcAddress`。Linux 与 macOS 使用 `dlopen` 和 `dlsym`，在打开时解析依赖，并将符号限制在库的加载范围内。
- 相对路径按当前目录解析。无效路径、加载失败、无效符号、符号缺失和对空句柄的访问通过 `std::expected` 返回错误。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 从空构建目录构建独立测试模块并通过 `platform-tests`。测试覆盖函数调用、移动、释放后重新加载、缺失文件和符号，以及带嵌入空字符的输入。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

游戏模块入口、ABI 检查和热重载由后续阶段在此接口上实现。
