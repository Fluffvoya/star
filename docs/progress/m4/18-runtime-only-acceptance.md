# M4-18 纯运行时构建与基础验收报告

- 日期：2026-11-27
- 分支：`build/m4-runtime-only`
- 状态：Linux x64 基础验收通过

## 本阶段交付

- `STAR_BUILD_ASSET_PIPELINE=OFF` 可关闭源资产工具、相关测试和导入样例。运行时资产读取、Renderer 和着色器构建仍然可用。
- 使用 vcpkg 工具链时，CMake 仅在启用资产工具时自动请求包含 fastgltf 的 `asset-pipeline` manifest 功能。

## Linux x64 验收结果

| 路径 | 结果 |
| --- | --- |
| 完整 Development 构建 | 16 项 CTest 全部通过，包括公共头独立编译 |
| 干净纯运行时构建 | 禁用 fastgltf 查找，12 项 CTest 全部通过 |
| 独立运行时消费者 | 读取派生模型并在 Vulkan llvmpipe 验证层下输出 960×640 RGBA 帧 |
| 派生数据一致性 | 同一贴图标准场景从源文件直导与从 DDC 载荷加载，两张读回图逐字节一致 |
| 固定场景渲染 | 验证深度遮挡、分簇光照、级联阴影、PBR、IBL、色调映射和 Lit/Unlit 变体 |
| 标准场景帧时 | Intel Core i7-14650HX、llvmpipe（LLVM 23.1.5）、960×640，贴图球阵 30 帧平均 GPU 时间 126.785 ms |

标准场景来源、文件校验值和分阶段数据见 [贴图材质报告](12-gltf-material-textures.md)、[类型化资产加载](16-typed-asset-loading.md) 和 [运行时 Renderer](17-runtime-forward-renderer.md)。

## 仍待验证或实现

- Windows x64、macOS arm64 和硬件 GPU 未在本轮验证。帧时为本地软件驱动基线。
- 静态模型与 RGBA8 纹理链路已验证。蒙皮、动画、压缩纹理和 Pak 路径仍待实现。
- 当前 Renderer 支持离屏静态场景。Scene/World 桥接、异步 GPU 上传、透明与遮罩材质仍待接入。
