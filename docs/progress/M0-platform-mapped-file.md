# M0 Platform 只读文件映射阶段报告

- 日期：2026-11-20
- 实现分支：`feat/m0-platform-mapped-file`
- 范围：普通文件的只读映射、字节视图和映射生命周期

## 已交付

- `MappedFile::open()` 映射整个普通文件，`getBytes()` 返回借用的只读字节视图。空文件返回空视图，映射所有权可移动，析构时释放视图对应的系统资源。
- Windows 使用 `CreateFileMappingW` 和 `MapViewOfFile`。Linux 与 macOS 使用 `mmap`。SDL 初始化与文件映射相互独立。
- 无效路径、缺失文件、非常规文件和系统调用失败通过 `std::expected` 返回错误码。调用方须在使用映射期间保持文件大小和内容稳定。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增用例覆盖字节读取、空文件、移动构造和赋值、目录、缺失文件及带嵌入空字符的路径。
- 公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

后续 VFS 和资产读取可复用只读映射。文件监视仍是独立的 Platform 工作。
