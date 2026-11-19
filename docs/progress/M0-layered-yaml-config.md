# M0 Core YAML 分层配置阶段报告

- 日期 2026-11-19
- 实现分支 `feat/m0-layered-yaml-config`

## 已交付

- `Config::load()` 依次读取引擎、项目、平台和用户 YAML 文件，再应用 `--set=key=value` 参数。调用方以空路径跳过可选层，后读取的同名键覆盖此前的值。
- 文件支持一个由嵌套映射和标量组成的 YAML 文档。配置通过点分隔的键查询。单层内的重复键、序列、空值、标签、锚点和别名返回错误。解析错误包含配置层及可用的行列信息。
- `Config::applyCVars()` 读取 `CVars` 映射，并按已注册的 CVar 类型转换文本。注册表先验证整个批次再更新，任何未知变量或无效值都会拒绝整批更新。
- Core 私有静态链接 MIT 许可的 rapidyaml v0.15.2，子模块固定到 `85bfe56e32b1cfe715aa1c39885b1b10761e7482`，其递归依赖由上游提交固定。

## 验证

- Linux x64 的 `linux-debug`、`linux-dev`、`linux-profile` 和 `linux-shipping` 各通过 4 项 CTest。
- 新增测试覆盖五层覆盖顺序、保留其他设置、显式文档标记、重复键、语法错误、缺失文件、无效参数及 CVar 批量回滚。
- `core-tests` 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer。`core/config.hpp` 通过独立编译，Linux 动态 Core 的 rapidyaml 和 c4core 符号均维持私有可见性。
- `clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

配置文件路径由调用方指定。启动器接入时再集中提供默认路径，并在相应宿主中验证 CLI 行为。正式发布使用的 vcpkg 清单和静态依赖供应仍待落地。
