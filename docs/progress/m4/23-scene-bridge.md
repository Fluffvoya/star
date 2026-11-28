# M4-23 场景渲染桥接、相机灯光与透明材质

- 日期：2026-11-28
- 分支：`feat/m4-closure`
- 状态：Linux x64 Development 验证通过；Windows x64 与 macOS arm64 本轮未复验

## 本阶段交付

- 新增 `renderer::RenderScene`：模型几何、逐部件阴影标记、相机列表、方向光、点光（上限 64）和几何包围球。`scene::StaticRenderSnapshot` 成为它的别名，Scene 依赖 Renderer，渲染器仍不接触 GameObject。
- `extractStaticRenderSnapshot` 在烘焙世界变换的同时累计包围球，采集相机（世界矩阵、竖直视场、近远平面）、首个方向光和点光。非法相机或灯光分别返回 `InvalidCamera`、`InvalidLight`，不再静默跳过。
- `Light` 组件新增 `directional`。方向光按对象 +Z 轴给出朝向光源的方向（对象前向仍为 -Z），点光位于对象原点。
- `ForwardRenderer::create(const RenderScene&, ...)` 使用场景相机与灯光；`setCamera` 与 `setLights` 在不重建几何的前提下逐帧替换视图、级联阴影和灯光，供编辑器视口与 PIE 直接复用。
- 透明与遮罩材质：模型资产格式升级到 v2，逐材质记录 `blend` 与 `alpha_cutoff`，v1 载荷仍可读取；glTF `alphaMode` 映射为 Opaque / Masked / Transparent。
- 渲染拆分为三条颜色管线：不透明使用预通道深度相等测试；遮罩跳过预通道，在颜色通道里按 `alpha_cutoff` 丢弃并写深度；透明按部件中心到相机的距离从远到近排序，混合且不写深度。预通道与阴影通道跳过透明部件，遮罩部件因阴影管线没有片元着色器而按实体投影。
- 着色器输出基础色 alpha，并在 `factors.z > 0` 时按裁切值丢弃；透明混合依赖 `shaderDemoteToHelperInvocation`，设备要求与创建时启用一并补齐。
- 新增样例 `star-scene-forward`：构造 World/Scene（地面、三个不透明盒、一块透明面板、方向光与两盏点光），提取快照后逐帧旋转相机、调整灯光强度并渲染。新增本地 Vulkan 冒烟测试 `vulkan-scene-forward`，无可用设备时以退出码 77 跳过。

## 验证结果

环境：Linux x64、GCC、llvmpipe（LLVM 23.1.5）、`VK_ICD_FILENAMES` 指向本地 lavapipe，全部渲染开启 Khronos 验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 干净重建 | 通过 |
| CTest | 15 项通过，`vulkan-scene-forward` 无设备时标记为 Skipped |
| CTest（挂载 lavapipe） | 16/16 通过，`vulkan-scene-forward` 执行 |
| 场景快照专项 | 相机矩阵/视场/近远平面、方向光朝向、点光位置与范围、包围球、点光上限、非法相机与非法灯光均覆盖 |
| 模型资产往返 | v2 的 `blend` 与 `alpha_cutoff` 往返一致；v1 载荷按不透明默认值读取 |
| 场景渲染 | 960×640，5 个部件，透明面板后方几何可见，方向光阴影与两盏彩色点光表现正常 |
| 重复渲染 | 同参数两次运行读回 PPM 逐字节相同 |
| 相机动画 | 帧数变化改变读回图像，`setLights` 的强度脉冲不破坏渲染 |
| 模型输入回归 | `star-vulkan-forward` 固定 OBJ 场景的 PPM 与改动前逐字节一致 |

场景样例在 llvmpipe 上 7 帧平均 GPU 时间 8.5 ms，含透明混合与逐帧相机/灯光更新；软件驱动数值只用于本机回归。

## 阶段边界

- 逐实例材质覆盖（`MeshRenderer.materials`）尚未接入：运行时 `AssetManager` 目前只解码纹理与静态模型，材质资产需要新增解码与参数映射。
- 纹理仍为 RGBA8：BC 压缩与 HDR/EXR/KTX2 导入未实现，纹理管线只覆盖 LDR PNG/JPEG/TGA/BMP/PSD。
- 掩罩材质的阴影没有 alpha 测试，按实体轮廓投影；透明部件不投影。
- 透明排序按部件中心，不按三角形排序，交叠的透明面片可能出现顺序误差。
- 渲染器实现仍直接使用 Vulkan，尚未通过 RHI；RHI 缺管线、采样器、描述符组、栅栏与交换链，需在 M5 的 ImGui 后端前补登记。
- Windows x64 与 macOS arm64 未在本轮复验；显卡实机未覆盖，以上结果来自本地软件 Vulkan。
