# ADR-0017：构建产物形态

- 状态：已确认
- 日期：2026-11-20
- 关联：[Star Engine 实现方案](../architecture/implementation-plan.md) §3.3、§4.2；[ADR-0006](0006-build-and-dependencies.md)、[ADR-0009](0009-game-code-iteration.md)、[ADR-0018](0018-implementation-baseline.md)

## 背景

编辑器开发需要独立重新编译和加载游戏模块，发布版需要减少动态库部署问题并允许跨模块优化。单一库形态无法同时满足两类工作流。

## 备选方案与后果

- 所有配置都使用动态库。热重载方便，但发布时需要处理库搜索路径、ABI 和依赖部署。
- 所有配置都使用静态库。发布简单，但编辑器和游戏模块无法独立重载。
- 开发配置动态模块，Profile/Shipping 静态单体。需要维护两种 CMake 配置，但分别满足迭代和发布目标。

## 决定

Debug/Development 使用动态引擎模块，游戏代码在编辑器开发场景中使用动态库。Profile/Shipping 使用静态引擎模块和静态单体链接，编辑器不进入发布产物。库类型在配置目录创建时由 `STAR_MONOLITHIC` 确定，不在同一 target 内动态切换。

所有配置保持一致的公共 API、资源生命周期和编译标准。跨 DLL 分配使用统一的分配入口，发布版关闭编辑器专用代码。

## 后果

构建目录不能混用不同库形态或 ABI。游戏模块热重载只适用于动态库配置。发布构建必须同时验证依赖、链接、启动和运行时资源查找。

## 验收边界

M0 提供 Debug/Development 与 Profile/Shipping 独立预设。M5 验证动态库热重载。M9 验证静态发布包在无开发环境机器上的启动。
