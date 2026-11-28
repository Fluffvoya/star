# M4-26 里程碑收尾

- 日期：2026-11-28
- 分支：`feat/m4-completion`
- 状态：M4 在 Linux x64 收尾；Windows x64 与 macOS arm64 的最新改动未复验

## 本阶段交付

- 材质资产支持贴图引用：`BaseColorTexture`、`MetallicRoughnessTexture` 解析为纹理 GUID，快照按需求值并合并进场景纹理表，逐实例材质可以换贴图。
- 材质状态接入渲染：`Shading`（Lit/Unlit）与 `DoubleSided` 成为逐部件属性，模型资产格式升级到 v3（v1/v2 仍可读）；glTF 的 `unlit` 与 `doubleSided` 正确映射。
- 渲染器改为按材质状态选择管线：着色模型 × 单双面 × 不透明/遮罩/透明共 12 条颜色管线，深度与阴影各两条。透明部件仍按距离从远到近绘制，管线切换按需绑定。
- `DepthTest: false` 不再被静默忽略，材质解析返回 `UnsupportedProperty`，快照报 `InvalidMaterial`。
- `star-scene-forward` 增加一个 Unlit、双面的绿色面板，端到端验证逐部件着色模型与管线选择。
- 新增 [ADR-0019](../adr/0019-renderer-vulkan-boundary.md)（提议）：记录 M4 期间渲染器直接使用 Vulkan 的边界、退出条件，以及 M7 前必须收敛的要求。

## M4 验收核对

对照实现方案 §20 的 M4 交付与验收：

| 方案条目 | 结果 | 证据 |
| --- | --- | --- |
| 资产数据库、`.meta` | 完成 | [01](01-asset-database.md) |
| 导入器：纹理、glTF、FBX/OBJ | 完成 | [03](03-texture-import.md)、[05](05-static-model-import.md)、[12](12-gltf-material-textures.md) |
| DDC、异步加载、引用计数 | 完成 | [02](02-derived-data-cache.md)、[04](04-asset-loading.md)、[16](16-typed-asset-loading.md) |
| Forward+：深度预通道、分簇光照 | 完成 | [07](07-forward-rendering.md)、[08](08-clustered-lighting.md) |
| CSM 阴影、PBR、IBL、色调映射 | 完成 | [09](09-cascaded-shadows.md)、[10](10-image-based-lighting.md) |
| 材质系统第一版（参数 + 变体） | 完成 | [06](06-material-assets.md)、[14](14-material-variants.md)、[24](24-material-overrides.md)、本阶段 |
| 固定标准场景、参考机器与帧时 | 完成 | [11](11-standard-render-scene.md)、[19](19-windows-acceptance.md)、[20](20-discrete-gpu-selection.md) |
| 场景到渲染器、相机与灯光 | 完成 | [23](23-scene-bridge.md) |
| 纹理块压缩（颜色链路） | 完成 | [25](25-block-compressed-textures.md) |

## 验证结果

环境：Linux x64、GCC、llvmpipe（LLVM 23.1.5），渲染开启 Khronos 验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 干净重建 | 通过 |
| CTest | 17/17 通过（含 `vulkan-scene-forward`） |
| 材质贴图与状态 | 贴图 GUID 解析并合并进纹理表、索引正确；Unlit 与双面生效；`DepthTest: false` 被拒绝 |
| 模型资产往返 | v3 的混合、着色模型与双面设置往返一致 |
| 场景渲染 | 7 个部件：不透明盒、金属盒、透明面板、Unlit 绿色面板、方向光阴影与两盏点光 |
| 重复渲染 | 同参数两次运行读回逐字节一致 |
| 固定 OBJ 场景 | 读回图与上一版本不同：逐部件双面状态取代了全局双面，画面内容一致（地面为单面网格，仍按变体默认双面绘制） |
| Khronos 贴图球阵 | BC1 压缩贴图渲染正常，两次运行一致 |

帧时（960×640，llvmpipe，仅本机参考）：固定 OBJ 场景约 4–6 ms，场景样例（7 部件）约 10 ms，Khronos 贴图球阵约 133 ms。

## 里程碑边界

M4 已按方案 §20 的交付与验收完成。以下条目属于方案后续章节或消费者尚未落地的功能，不计入 M4 缺口：

- BC4/BC5/BC6H/BC7：当前渲染器没有法线贴图、遮罩纹理或 HDR 环境贴图的消费者；颜色链路已由 BC1/BC3 覆盖。等法线贴图与 HDR 环境接入时一并实现。
- HDR/EXR/KTX2/DDS 输入：同上，当前 IBL 使用内置程序化环境。
- 独立 copy 队列、环形暂存与 mip 流式上传：静态上传路径已验证；流式属于运行时资产加载的后续工作。
- 蒙皮与动画（M8）、Pak 与烘焙（M9）、窗口与交换链、编辑器（M5）。
- 渲染器与 RHI 的边界见 [ADR-0019](../adr/0019-renderer-vulkan-boundary.md)（提议），需在 M5 的 ImGui 后端前落定。
- Windows x64 与 macOS arm64 未复验；显卡实机未覆盖，以上渲染结果来自本地软件 Vulkan。

## 复验

```bash
cmake --preset linux-dev -DSTAR_SLANGC=$(command -v slangc) -DSTAR_TEST_VULKAN=ON
cmake --build build/linux-dev
VK_ICD_FILENAMES=<lavapipe icd> LD_LIBRARY_PATH=<lavapipe lib> ctest --test-dir build/linux-dev
```
