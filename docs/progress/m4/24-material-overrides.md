# M4-24 逐实例材质覆盖

- 日期：2026-11-28
- 分支：`feat/m4-material-overrides`
- 状态：Linux x64 Development 验证通过；Windows x64 与 macOS arm64 本轮未复验

## 本阶段交付

- `AssetManager` 新增 `MaterialAsset` 解码：`registerAsset<MaterialAsset>` / `load` 走后台任务读取 `.mat`，用 `serialization::readYaml` 与 `readMaterial`（`Preserve` 策略）解析，未知字段保留。
- 新增 `resolveMaterial`：把 `BaseColor`、`Metallic`、`Roughness` 解析为渲染器直接消费的 `ResolvedMaterial`；缺失字段取默认值，已知字段形状错误或超出 0–1 范围返回 `InvalidInput`，未知参数忽略。
- `extractStaticRenderSnapshot` 读取 `MeshRenderer.materials`：单个材质覆盖该对象的全部部件，材质数量与部件数一致时逐部件覆盖，其他数量返回 `InvalidMaterial`；覆盖保留模型自带的纹理槽，只替换基础色、金属度、粗糙度与混合模式。
- `star-scene-forward` 增加一个通过材质资产着色的金属盒，端到端覆盖“材质资产 → 资产管理器 → 快照 → 渲染器”。

## 验证结果

环境：Linux x64、GCC、llvmpipe（LLVM 23.1.5），全部渲染开启 Khronos 验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 干净重建 | 通过 |
| CTest | 17/17 通过（含 `vulkan-scene-forward`） |
| 材质解码 | `.mat` 经 `AssetManager` 解码后着色器、混合模式、参数与源文件一致 |
| 参数解析 | BaseColor 通道数与范围、Metallic/Roughness 范围错误均被拒绝；未知参数保留 |
| 逐实例覆盖 | 单材质覆盖全部部件；数量不匹配返回 `InvalidMaterial`；覆盖后的基础色、金属度、粗糙度、混合模式符合材质资产 |
| 场景渲染 | 6 个部件，金属盒外观与场景自带材质明显不同，透明面板与阴影表现不变 |

## 阶段边界

- 覆盖范围是标量参数与混合模式。`Shading`、`DoubleSided`、`DepthTest` 仍是全局变体设置，尚未按材质切换管线。
- 材质里的纹理引用（贴图 GUID）尚未解析，材质替换不会换贴图。
- 运行时材质解码读取的是编辑期 YAML；Shipping 需要烘焙后的二进制材质格式与对应解码分支。
- Windows x64、macOS arm64 与显卡实机未复验。
