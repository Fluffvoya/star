# M4-05 静态模型导入报告

- 日期：2026-11-27
- 分支：`feat/m4-static-model-import`
- 状态：Linux x64 本地验证完成

## 本阶段交付

- 工具模块使用 fastgltf 0.8.0 读取 glTF/GLB，使用固定版本 ufbx 读取 FBX/OBJ。接入细节见 [构建要求](../../../README.md) 和 [ufbx 说明](../../../engine/source/third-party/ufbx/README.md)。
- 导入结果提供带索引的三角形、位置、法线、第一套 UV 和材质槽名称。实例变换烘焙到结果中，输出采用右手系、Y 向上、米为单位和左上原点 UV。
- glTF 外部缓冲通过调用方提供的解析器读取。导入前检查缓冲长度和访问器范围，拒绝越界索引、路径穿越和当前无法处理的变形数据。
- FBX/OBJ 面按材质槽拆分并三角化，生成缺失的法线，合并相同顶点。

## 验证结果

- Linux x64 Development 使用 vcpkg 基线的 fastgltf 和 simdjson 构建，全部 15 项 CTest 通过。
- 专项验证覆盖 FBX 厘米到米的转换和位移、OBJ 四边形三角化、GLB 层级和镜像变换、外部 glTF 缓冲、截断和非法路径。
- Windows x64 与 macOS arm64 尚未验证。

## 阶段边界

- 当前交付静态几何和材质槽关联。材质参数与贴图、稀疏访问器、蒙皮、动画、切线、LOD 和 DDC 集成属于后续工作。
