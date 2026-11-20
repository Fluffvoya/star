# M1-01 反射基础阶段报告

- 日期：2026-11-20
- 实现分支：`task/m1-reflection-foundation-20261120`
- 状态：Linux x64 本地验证完成，M1 序列化阶段尚未开始

## 本阶段交付

- 新增 `StarReflection` 运行时模块，提供稳定的类型 ID、类型元数据、属性访问器、类型操作表和模块注册表。
- 注册表支持按类型 ID 或全名查询、基类派生类型查询、模块注册和模块卸载，并检查重复 ID、所有者和无效元数据。
- 新增 `reflection/annotations.hpp`，提供 StarHeaderTool 使用的 `STAR_CLASS`、`STAR_STRUCT`、`STAR_ENUM`、`STAR_PROPERTY` 和 `STAR_FUNCTION` 标记宏。
- 新增 `StarHeaderTool`。工具使用 libclang 解析 C++23 头文件，生成稳定排序的 `.gen.hpp` 元数据文件，并把直接输入、递归 include 依赖、include 路径和工具版本纳入缓存指纹。
- 生成器只在输出内容变化时写文件，缓存命中时跳过解析。直接头文件和递归 include 文件变化都会使缓存失效。
- 新增 Reflection 独立公共头检查和注册表、继承查询、属性类型擦除访问测试。

## Linux x64 验收

执行命令：

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

结果：7/7 测试通过，包含 `hello-window-headless`、Core、Reflection、Jobs、Platform、公共头独立编译和工作线程配置测试。

StarHeaderTool 使用临时头文件完成以下验证：

- 识别带 `STAR_CLASS` 和 `STAR_PROPERTY` 标记的类型与字段。
- 首次生成输出和第二次缓存命中均返回成功，第二次运行不改写输出文件。
- 修改递归 include 的依赖头文件后，缓存指纹变化并重新生成。

## 阶段边界

本阶段没有实现 YAML Archive、二进制 Archive、反射驱动的递归容器访问、未知字段保留和版本升级。它们属于后续 M1 阶段，当前报告不将这些功能标记为已交付。

当前只在 Linux x64 上验证。Windows x64 和 macOS arm64 的 libclang 发现、生成器构建和运行仍待对应主机验证。
