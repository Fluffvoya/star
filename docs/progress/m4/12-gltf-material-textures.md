# M4-12 glTF PBR 贴图材质报告

- 日期：2026-11-27
- 分支：`feat/m4-gltf-material-textures`
- 状态：Linux x64 软件 Vulkan 本地验证完成

## 本阶段交付

- 新增带贴图的 glTF 静态导入入口，支持嵌入与外部图像。基础色和金属度／粗糙度走不同色彩空间，按图像及用途复用导入结果。
- Vulkan 样例上传完整 mip 链。基础色按 sRGB 采样，金属度／粗糙度保持线性，从 G、B 通道读取，再与材质因子相乘。
- 缺省贴图使用白色中性值。只导入几何的入口继续独立可用。

## 验证结果

- 使用 Ed Mackey 制作、Analytical Graphics, Inc. 以 CC BY 4.0 发布的 [Metal-Rough Spheres 贴图场景](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/MetalRoughSpheres)。本地 GLB 的 SHA-256 为 `450c05557b0823a10835e32c05caf77a7b175eba94516555c1c9bb084f563f01`。
- Intel Core i7-14650HX、llvmpipe（LLVM 23.1.5）、Slang v2026.18.2，启用 Vulkan 验证层。以 960×640 渲染五个网格部件，读回图中可见基础色与粗糙度变化。
- 预热 339.055 ms，随后 30 帧平均 GPU 时间 136.730 ms，包含阴影、深度、分簇、着色和读回拷贝。
- 固定无贴图 OBJ 的输出与加入贴图绑定前逐字节一致。全部 15 项 CTest 通过。
- Windows x64、macOS arm64 和硬件 GPU 尚未验证。

## 阶段边界

- 样例使用 TEXCOORD_0 与统一的线性采样器。glTF 采样器状态、法线、Alpha 遮罩与混合、DDS/KTX2/EXR 和 BC 压缩属于后续工作。
