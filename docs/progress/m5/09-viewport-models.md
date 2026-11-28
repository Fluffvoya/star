# M5-09 视口模型预览

- 分支：`feat/m5-viewport-models`
- 验证环境：Linux x64、`linux-dev`、llvmpipe Vulkan。Windows x64、macOS arm64 未验证。

## 交付

- 场景中 `MeshRenderer.mesh` 的 GUID 通过项目 `Content/` 中的 `.meta` 定位源模型。预览复用 OBJ、FBX 和 glTF 导入器，使用 `Intermediate/DDC/<平台>/` 缓存派生数据。依赖外部资源的文本 glTF 直接导入，且只解析项目 Content 内的资源。
- 编辑器相机及 PIE 游戏相机共用 RHI 模型预览。网格、索引资源和深度附件直接记录进视口渲染通道。拖动操纵器时，模型与预览姿态同步。
- Content 面板的 Refresh 可重新扫描并导入。找不到资产或上传失败时，控制台报告对应 GUID，其他已加载的模型可以继续显示。

## 验证

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建及软件 Vulkan CTest | 19/19 通过 |
| RHI 深度附件 | 启用深度附件的离屏清屏通过像素回读 |
| 编辑视口 | 临时工程通过 `.meta` 加载同一 OBJ 的两个实例，截图显示遮挡正确。同步验证下绘制 30 帧，检查生成了 DDC 载荷。 |

模型预览使用基础色及简单光照，尚未接入 Forward+ 材质贴图、场景灯光和阴影。首次导入在编辑线程执行。本次实际视口验证使用 OBJ，FBX 及 glTF 由现有导入器测试覆盖。
