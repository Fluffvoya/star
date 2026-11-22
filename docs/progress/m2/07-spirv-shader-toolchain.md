# M2-07 SPIR-V 着色器工具链烟测阶段报告

- 日期：2026-11-22
- 分支：`task/m2-phase-01-20261122`
- 状态：Linux x64 本地验证完成，Slang 集成尚未开始

## 本阶段交付

- 新增 `rhi/shader_compiler.hpp`，提供顶点、片元和计算阶段选择、编译选项和错误类型。
- 通过 `glslc` 将 GLSL 源码编译为 SPIR-V，使用临时文件隔离输入和输出。
- 可选调用 `spirv-val` 校验模块，读取并检查 SPIR-V magic，返回内存中的 32 位字模块。
- 编译器、验证器、文件读写和无效 SPIR-V 均返回可诊断错误，不把工具失败当成成功。
- 增加 GLSL 顶点着色器编译与校验测试，并允许目标主机缺少工具时返回 `CompilerUnavailable`。

## 验证结果

Linux x64 `linux-dev` 预设的完整构建和 CTest 共 11 项全部通过。当前主机使用 `/usr/bin/glslc` 和 `spirv-val` 成功生成并校验 SPIR-V。

## 阶段边界

本阶段使用 GLSL 工具链验证 SPIR-V 数据路径，尚未集成方案要求的 Slang、反射布局输出、Shader 变体和管线创建。编译接口通过外部工具进程工作，尚未接入编辑器资产缓存。Windows x64 和 macOS arm64 尚未验证。
