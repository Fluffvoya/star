# M4-07 Vulkan 前向渲染验证报告

- 日期：2026-11-27
- 分支：`feat/m4-forward-rendering`
- 状态：Linux x64 软件 Vulkan 本地验证完成

## 本阶段交付

- 新增离屏 `star-vulkan-forward` 样例，使用 AssetPipeline 导入静态几何并提交索引绘制。
- Vulkan 深度预通道先写入深度。前向着色以相等深度测试复用该数据，使用 GGX 镜面、Lambert 漫反射和金属度/粗糙度参数，输出经 Reinhard 色调映射的 sRGB 图像。
- 样例可读回 PPM，并记录包含深度预通道、着色和读回拷贝的 GPU 时间。

## 验证结果

- Linux x64 Development，Intel Core i7-14650HX，Vulkan llvmpipe（LLVM 23.1.5），Slang v2026.18.2。启用 Vulkan 验证层，编译产物通过 SPIR-V 验证。
- 使用仓库内固定场景，在 960×640 渲染四个网格部件。检查读回图像，地面和三个部件的遮挡与表面明暗符合预期。
- 预热 58.552 ms，随后 30 帧的平均 GPU 时间为 0.592 ms。这个测量记录软件驱动及当前场景的基线。
- Vulkan 构建中全部 15 项 CTest 通过。Windows x64、macOS arm64 及硬件 GPU 尚未验证。

```sh
build/m4-vulkan/bin/star-vulkan-forward samples/vulkan-forward/forward-scene.obj /tmp/star-forward.ppm 30
```

## 阶段边界

- 当前使用一个方向光和样例材质值。分簇光照、CSM、IBL、运行时材质绑定和大规模标准场景验收属于后续工作。
