# M0 Core CVar 阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-core-cvars`
- 范围：Core 运行时变量的注册、查询与修改

## 已交付

- `CVar<T>` 支持 `bool`、`int`、`float` 和 `std::string`。实例创建时注册名称、默认值、说明和标记，销毁时解除注册。
- `CVarRegistry::getInfo()` 按名称读取元数据与当前值。`setFromString()` 将文本转换为变量的原有类型并立即更新，供后续控制台与配置加载使用。
- 名称区分大小写，由点分隔的标识符组成。布尔值接受 `true`、`false`、`1`、`0`，数值按完整文本解析，字符串保留原文。无效名称、重复注册、未知变量和无效值以 `std::expected` 返回。
- 运行时注册表拥有数据副本，支持并发读取与修改。使用类型化实例的线程应在实例销毁前结束访问。再次注册同名变量时使用新实例的默认值。

## 验证

- Linux x64 的 `linux-debug`、`linux-dev`、`linux-profile` 和 `linux-shipping` 均完成配置与构建，各通过 4 项 CTest。
- 新增 Core 用例覆盖静态注册、按类型和按名称访问、默认值与说明、重复注册、解除注册、文本转换、失败后保留旧值和并发访问。
- `core-tests` 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer。`core/cvar.hpp` 通过独立语法编译。
- `clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

接入固定版本的 YAML 解析依赖，完成配置文件分层加载和命令行覆盖，再把配置中的值应用到 CVar。控制台 UI 随编辑器阶段接入。
