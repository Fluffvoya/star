# M1 Windows x64 验收报告

- 复验日期：2026-11-26
- 主机工具链：Windows x64（Build 26300）、MinGW-w64 GCC 16.2.0、CMake 4.4.2、Ninja 1.13.2、libclang/Clang 23.1.6
- 依赖：仓库 vcpkg 清单

| 预设 | 链接模式 | vcpkg 目标 triplet | M1 CTest |
| --- | --- | --- | --- |
| `win-dev` | 引擎 DLL | `x64-mingw-dynamic` | 4/4 通过 |
| `win-profile` | 静态单体 | `x64-mingw-static` | 4/4 通过 |

两种配置均在独立构建目录中从头生成并编译反射代码，执行了 `reflection-tests`、`serialization-tests`、`generated-reflection` 和 `generated-reflection-incremental`。测试验证类型和旧字段名称、属性与归档往返，以及修改传递头文件后的重生成和缓存恢复。

Windows 上需要将 LLVM 的头文件、导入库和 DLL 加入构建或运行环境。本次配置显式提供 `STAR_LIBCLANG_INCLUDE_DIR`、`STAR_LIBCLANG_LIBRARY` 和 `STAR_CLANG_DRIVER`，并将 LLVM `bin` 加入 `PATH`。StarHeaderTool 规范化 Clang 资源目录的路径分隔符。MinGW 构建将目标三元组和 sysroot 传给 libclang，修复解析器找不到 C++ 标准库头文件的问题。

本报告覆盖 Windows x64 的 MinGW Development 和 Profile。MSVC 与 macOS arm64 尚未在本轮验证。
