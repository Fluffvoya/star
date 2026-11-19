# 代码风格规则

适用于手写代码和文档中的代码示例。

- 以仓库的 [`.clang-format`](../.clang-format)、[`.clang-tidy`](../.clang-tidy) 和 [`.editorconfig`](../.editorconfig) 为准。采用 LLVM 基线、四空格缩进、Tab 宽度 4、120 列；不用硬 Tab 缩进。
- 源码注释、API 文档注释和代码示例注释使用英文。独立设计文档可以使用中文。
- 只格式化本任务涉及的手写代码，不重新格式化第三方源码或生成文件。

## 命名

- 文件夹使用 kebab-case：全小写，单词之间用短横线分隔，例如 `runtime-jobs/`、`window-events/`。
- C++ 源文件和头文件使用 snake_case，例如 `job_scheduler.hpp`、`window_events.cpp`。
- 类和 concept 使用 UpperCamelCase（大驼峰），例如 `FrameClock`、`JobCallable`。
- 除构造函数、析构函数和运算符重载等语言固定形式外，所有函数使用 lowerCamelCase（小驼峰），包括自由函数、成员函数和静态成员函数，例如 `submitJob()`、`isComplete()`。
- 命名空间使用 snake_case，例如 `star::runtime_core`。
- 变量使用 snake_case，例如 `pending_job_count`、`frame_index`。
- 私有成员变量添加 `m_` 前缀，并延续 snake_case，例如 `m_pending_job_count`、`m_window_handle`。
- 组件及其派生类声明为 `struct`，例如 `struct TransformComponent`。
- `GameObject` 及其派生类声明为 `class`，例如 `class Actor`。
