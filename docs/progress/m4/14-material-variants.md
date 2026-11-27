# M4-14 材质着色器变体报告

- 日期：2026-11-27
- 分支：`feat/m4-material-variants`
- 状态：Linux x64 软件 Vulkan 本地验证完成

## 本阶段交付

- Vulkan 样例可读取 `.mat` 并校验着色器 GUID、渲染状态和已支持的参数。基础色、金属度和粗糙度可覆盖源模型材质值。
- 材质变体键选择分别编译的 Lit 或 Unlit 片元着色器。双面状态在阴影、深度预通道和前向绘制中统一设置。
- [Unlit 示例材质](../../../samples/vulkan-forward/unlit-example.mat) 可直接应用于固定样例场景。

## 验证结果

- Intel Core i7-14650HX、llvmpipe（LLVM 23.1.5）、Slang v2026.18.2，启用 Vulkan 验证层。两个变体都在 960×640 完成 30 帧，全部 15 项 CTest 通过。
- 同一地面样点的 Lit RGB 为 (171, 185, 201)，Unlit 读取覆盖颜色后为 (231, 118, 49)。两张图的背景样点一致，Unlit 表面不受灯光和阴影影响。
- 平均 GPU 时间为 Lit 3.770 ms、Unlit 0.855 ms。Windows x64、macOS arm64 和硬件 GPU 尚未验证。

```sh
build/m4-vulkan/bin/star-vulkan-forward samples/vulkan-forward/forward-scene.obj /tmp/star-unlit.ppm 30 --material=samples/vulkan-forward/unlit-example.mat
```

## 阶段边界

- 样例使用一个不透明材质覆盖和固定着色器集合。多材质 PSO 管理、透明或遮罩变体、Slang 动态反射属于后续工作。
