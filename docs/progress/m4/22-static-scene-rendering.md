# M4-22 静态场景提取与 Windows 复验入口

- 日期：2026-11-28
- 分支：`feat/m4-scene-rendering`
- 状态：Linux x64 软件 Vulkan 验证通过，Windows x64 新改动待实机复验

## 本阶段交付

- `extractStaticRenderSnapshot` 在 Scene 变换传播后采集可见的 `MeshRenderer`。快照按层掩码筛选对象，按 GUID 取得派生模型，并拥有独立的几何和贴图数据。
- 父子世界变换烘焙到顶点。非均匀缩放下变换法线，镜像实例翻转三角形绕序。同一模型的多个实例共享快照内的贴图。
- Renderer 使用快照的逐部件投影标志过滤阴影绘制。样例原有的模型输入和默认行为保持兼容。
- Windows 的 `windows-vulkan` CTest 标签在前向样例可用时包含三帧离屏测试，开启同步验证，产出 PPM。运行方式见 [Windows 构建指南](../../windows-build.md#启用-vulkan-专项验收)。

## 验证结果

- Linux x64 Development 干净构建通过全部 16 项 CTest，纯运行时构建通过全部 12 项 CTest。Scene 专项覆盖 GUID 缺失、层筛选、共享贴图、父子与镜像变换以及待销毁对象。
- 独立程序从 World 内的 Scene 提取派生模型，在 llvmpipe（LLVM 23.1.5）上渲染，并确认关闭对象投影会改变读回图。启用 Khronos Vulkan 验证层和同步验证，另验证上传提交后、首帧前销毁 Renderer 的路径。
- Khronos 贴图球阵在 960×640 输出的 PPM 与此前标准场景逐字节一致。Intel Core i7-14650HX 上的 llvmpipe 软件驱动 30 帧平均 GPU 时间为 128.782 ms。

## 阶段边界

- 本轮环境缺少原生 Windows 执行入口。此前 Windows x64 MinGW 实机结果见 [验收基线](19-windows-acceptance.md) 和 [双 GPU 复验](20-discrete-gpu-selection.md)。本轮结果以 Linux 实跑为准。
- 这是静态、按需重建的快照。Scene 相机、灯光、逐实例材质覆盖及每帧增量同步仍待接入。
- 蒙皮、动画及 BC 编码仍待补齐。上传队列的后续工作见 [静态上传报告](21-vulkan-static-upload.md)。
