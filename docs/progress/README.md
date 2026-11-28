# 里程碑阶段报告

| 里程碑 | 阶段索引 | Linux x64 验收 | Windows x64 MinGW 验收 |
| --- | --- | --- | --- |
| M0 | [基础设施](m0/README.md) | [总体验收](m0/23-completion.md) | [Windows 验收](m0/24-windows-acceptance.md) |
| M1 | [反射与序列化](m1/README.md) | [生成器与联合验收](m1/03-generated-reflection.md) | [Windows 验收](m1/04-windows-acceptance.md) |
| M2 | [RHI 与 Vulkan](m2/README.md) | [Vulkan 呈现与抓帧](m2/09-vulkan-textured-mesh.md) | [Windows 验收](m2/10-windows-acceptance.md) |
| M3 | [Gameplay 框架](m3/README.md) | [独立游戏模块与 Linux 验收](m3/07-game-module-acceptance.md) | 尚未验证 |
| M4 | [资产管线与 3D 渲染基础](m4/README.md) | [M4 收尾与验收核对](m4/26-m4-completion.md) | [Windows 实机基线](m4/19-windows-acceptance.md) |
| M5 | [编辑器第一版](m5/README.md) | 进行中（[03 编辑器文档与面板](m5/03-editor-documents.md)） | 尚未验证 |

各报告记录对应阶段的交付范围和实测结果。Windows 上复验时参照 [构建与测试指南](../windows-build.md)。

新增报告放在对应的里程碑目录，文件名沿用 `NN-topic-slug.md`，按阶段递增编号。标题标注里程碑和阶段，正文记录日期、实现分支、交付、验证结果和阶段边界，并更新该里程碑的索引。
