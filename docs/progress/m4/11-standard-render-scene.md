# M4-11 标准场景渲染验证报告

- 日期：2026-11-27
- 分支：`feat/m4-standard-render-scene`
- 状态：Linux x64 软件 Vulkan 标准场景验证完成

## 本阶段交付

- glTF 静态导入保留基础颜色、金属度和粗糙度因子。Vulkan 样例将这些数值送入 PBR 着色器。
- 样例支持正视相机，并按模型边界设置深度范围与点光源范围。标准球阵的毫米级模型尺寸可正确取景。

## 标准场景与结果

- 选用 Khronos glTF Sample Assets 的 [Metal-Rough Spheres 无贴图场景](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/MetalRoughSpheresNoTextures)。上游署名为 Krill Gavrilov，CAD 网格由 Open CASCADE Technology Draw Harness 脚本生成，使用 CC0-1.0。
- 本地 GLB 的 SHA-256 为 `5e677f260ec0f366967a6e9545de2f3dab2a160e307b9bcb9d050cd2028f63f8`。导入后渲染 123 个网格部件。
- Intel Core i7-14650HX、llvmpipe（LLVM 23.1.5）、Slang v2026.18.2，开启 Vulkan 验证层。在 960×640 读回图中，球阵的金属与非金属行表现出不同反射，沿粗糙度列高光逐渐扩散。源场景的金、灰两组球阵沿深度叠放，正视图中存在遮挡。
- 预热 458.867 ms，随后 30 帧平均 GPU 时间 289.241 ms，含阴影、预通道、分簇、前向着色与读回拷贝。固定小场景的阴影与间接光结果见 [级联阴影](09-cascaded-shadows.md) 和 [IBL](10-image-based-lighting.md) 报告。
- 全部 15 项 CTest 通过。Windows x64、macOS arm64 和硬件 GPU 尚未验证。

```sh
build/m4-vulkan/bin/star-vulkan-forward /tmp/star-m4-metal-rough-values.glb /tmp/star-m4-metal-rough-values.ppm 30 --front
```

## 阶段边界

- 本次标准场景覆盖静态几何、数值材质和样例渲染。贴图材质、着色器变体与运行时资产绑定仍需接入。
