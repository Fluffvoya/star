# M0-15 公共头文件独立编译阶段报告

- 日期：2026-11-20
- 实现分支：`test/m0-public-headers`
- 范围：Core、Jobs 和 Platform 的公共头文件检查

## 已交付

- `public-headers` 在 CTest 中逐个编译三个模块的公共 `.h`、`.hpp` 和生成的导出头。每个翻译单元只包含一个头文件。
- 每组检查通过对应的 `Star::` 模块目标继承消费者的编译定义和包含路径。预编译头和 Unity Build 在检查目标上关闭。
- CMake 跟踪 `include/` 中的头文件增删。也可以运行 `ctest --preset linux-dev -R '^public-headers$' --output-on-failure` 单独执行检查。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 均通过配置、构建和 5 项 CTest，独立编译检查覆盖 28 个头文件。
- 在独立副本中，于配置后新增一个引用未定义类型的公共头。CTest 自动发现该文件，并因预期的编译错误失败。
- `git diff --check` 通过。待验平台为 Windows x64 和 macOS arm64。
