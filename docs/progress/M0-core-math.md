# M0 Core 数学阶段报告

- 日期：2026-11-19
- 实现分支：`feat/m0-core-math`
- 范围：基础向量、四元数和矩阵类型

## 已交付

- `Vector2` 和 `Vector3` 支持加减、缩放、点积、二维叉积、三维叉积、长度和归一化。零向量归一化返回零向量。
- `Quaternion` 支持单位四元数、轴角构造、共轭和向量旋转。轴角旋转采用右手系约定。
- `Matrix4` 使用列向量和列主序存储，提供单位矩阵、平移、缩放、齐次向量变换和点变换。
- 提供 `Vec2`、`Vec3`、`Quat` 和 `Mat4` 别名，以及 `math::pi` 常量。当前实现为轻量基础层，后续可在别名层替换为 glm。

## 验证

- Linux x64 的 Debug、Development、Profile 和 Shipping 预设各完成配置、构建和 4 项 CTest。
- 新增测试覆盖向量运算和归一化、右手系 Y 轴旋转、列主序平移和缩放。
- Core 通过 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer。公共头独立编译，`clang-format --dry-run --Werror` 和 `git diff --check` 通过。
- 待验平台为 Windows x64 和 macOS arm64。

## 后续工作

补齐变换组合、投影和包围体等渲染辅助数学类型。若接入 glm，保持现有 `star::math` 别名和坐标约定。
