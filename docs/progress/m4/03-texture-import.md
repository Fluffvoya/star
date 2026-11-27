# M4-03 LDR 纹理导入报告

- 日期：2026-11-27
- 分支：`feat/m4-texture-import`
- 状态：Linux x64 本地验证完成

## 本阶段交付

- 工具模块通过固定版本 stb_image 从内存解码 PNG、JPEG、TGA、BMP 和合并图层 PSD，输出左上原点的 RGBA8。
- 可生成至 1×1 的 mip 链。面积加权 Box 滤波覆盖奇数尺寸的边缘。颜色数据在线性光空间滤波，法线数据在滤波后重新归一化，Alpha 以线性值滤波。
- 解码错误返回诊断。上游来源、版本和许可证见 [stb_image 说明](../../../engine/source/third-party/stb/README.md)。

## 验证结果

- Linux x64 Development 构建成功，全部 14 项 CTest 通过，包括公共头独立编译检查。
- 专项测试覆盖 PNG Alpha 保留、TGA 解码、奇数尺寸 mip、sRGB 与线性数据差异、法线归一化和无效输入。
- Windows x64 与 macOS arm64 尚未验证。

## 阶段边界

- HDR、EXR、DDS、KTX2、BC 压缩、DDC 载荷接入和 GPU 上传属于后续工作。
