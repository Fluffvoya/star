# M0 发布依赖与全局分配器阶段报告

- 日期：2026-11-20
- 实现分支：`task/m0-complete-20261120`
- 范围：Shipping 依赖清单、mimalloc 发现和进程级全局分配入口

## 已交付

- 添加 `vcpkg.json` 和 `vcpkg-configuration.json`，固定 vcpkg baseline，并声明 SDL3、mimalloc、glm 和 doctest 依赖。
- 添加 CMake 依赖入口。发现可用的 mimalloc CMake 目标时，Core 自动链接该目标并启用 `STAR_WITH_MIMALLOC`。
- 在单体构建中提供全局 `operator new/delete` 和对齐版本，统一转发到 `GlobalAllocator`。共享库构建不定义进程级替换，避免多个动态库各自替换分配入口。
- 系统分配路径改用 C 运行库分配函数，避免全局 `operator new` 与 `GlobalAllocator` 之间递归调用。零字节全局分配按 C++ 约定分配一个字节。
- mimalloc 不可用时，`STAR_ALLOCATOR=mimalloc` 继续通过 `GlobalAllocator` 返回 `Unavailable`，全局入口回退到系统分配器。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设均完成配置、构建和 6 项 CTest。
- `hello-window-headless`、Core、Jobs、Platform、公共头独立编译和工作线程配置测试全部通过。
- `clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- Windows x64 和 macOS arm64 尚未在当前主机实机验证。

## 后续工作

在 Windows x64 和 macOS arm64 主机使用同一 vcpkg baseline 完成本地构建，并将平台结果合并到 M0 总体验收报告。
