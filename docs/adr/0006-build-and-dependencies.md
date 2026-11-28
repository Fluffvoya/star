# ADR-0006：构建与依赖

- 状态：已确认
- 日期：2026-11-20
- 关联：[Star Engine 实现方案](../architecture/implementation-plan.md) §2.1、§4.2、§4.3；[ADR-0017](0017-build-artifacts.md)、[ADR-0018](0018-implementation-baseline.md)

## 背景

引擎、工具和独立游戏项目需要统一构建入口，同时兼顾 Linux/macOS 的开发便利和发布依赖的可复现性。

## 备选方案与后果

- 为各平台维护原生 IDE 工程。平台体验好，但配置分叉和模块依赖漂移明显。
- 只使用 vcpkg。版本容易固定，但 Linux/macOS 日常开发成本和系统集成成本增加。
- CMake + Ninja，开发时系统包优先，发布时固定 vcpkg。需要维护两套依赖查找策略，但能平衡开发效率和发布可移植性。

## 决定

统一使用 CMake 3.28+ 和 Ninja，不维护 MSBuild 或 Xcode 构建链路。Linux/macOS 开发构建优先系统包，缺失或版本不满足时回退 vcpkg。Windows 使用 vcpkg。Profile/Shipping 使用固定 baseline 的 vcpkg 依赖和平台静态 triplet。

依赖策略分为 `SYSTEM_THEN_VCPKG`、`VCPKG_ONLY` 和 `VENDORED`。API 不稳定的库固定使用 vcpkg 或仓库内固定源码。每个依赖记录用途、版本来源、许可证和目标配置。

EnTT 采用 VENDORED 策略；各平台从上游 Git 子模块使用固定的 3.14.0 版本（MIT），仅供 Scene 私有实现。

## 后果

CMake 预设必须为每个正式平台提供独立构建目录。发布配置遵循每个依赖的固定版本策略。依赖缺失或版本不满足时配置阶段失败并给出诊断。

## 验收边界

M0 提供三平台同名配置、构建和测试预设。发布依赖验收必须确认实际使用 vcpkg toolchain、baseline 和目标 triplet，并记录配置缓存中的依赖来源。
