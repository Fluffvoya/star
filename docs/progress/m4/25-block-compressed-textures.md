# M4-25 纹理块压缩

- 日期：2026-11-28
- 分支：`feat/m4-bc-textures`
- 状态：Linux x64 Development 验证通过；Windows x64 与 macOS arm64 本轮未复验

## 本阶段交付

- 纹理资产格式升级到 v2，记录 `TextureFormat`：`Rgba8`、`Bc1`、`Bc3`。mip 载荷按格式计算大小（BC 为 4×4 块、按行优先），v1 载荷按 `Rgba8` 读取。
- 新增 BC1 与 BC3 编码器（`asset-pipeline/texture_compress`）：BC1 用块内 RGB 最小/最大值构造两端点，四色插值选最近索引；BC3 在颜色块前加 BC4 风格的 alpha 块（8 级插值、3 位索引）。边缘块按重复边缘像素补齐。
- 纹理导入默认块压缩：全不透明的颜色/线性纹理输出 BC1，含 alpha 的纹理输出 BC3；法线用途保留 RGBA8，等待 BC5。导入设置与 `.meta` 增加 `BlockCompress` 开关，旧缓存载荷因格式不符自动重建。
- glTF 的基础色与金属粗糙度纹理走压缩路径；派生数据缓存的键与命中校验都包含格式期望。
- 渲染器按格式选择 `VK_FORMAT_BC1_RGBA_*` / `VK_FORMAT_BC3_*`（颜色空间决定 sRGB 与否），并按格式校验 mip 载荷长度。

## 验证结果

环境：Linux x64、GCC、llvmpipe（LLVM 23.1.5），渲染开启 Khronos 验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 干净重建 | 通过 |
| CTest | 17/17 通过（含 `vulkan-scene-forward`） |
| 编码器 | 不透明图输出 BC1（4×1 图 8 字节），含 alpha 输出 BC3（1×1 图 16 字节），测试内解码后的颜色与 alpha 与源一致 |
| 法线用途 | 保持 RGBA8，不压缩 |
| 资产往返 | BC1 mip 写入/读回一致，载荷长度错误被拒绝；v1 载荷按 RGBA8 读取 |
| glTF 导入 | 基础色与金属粗糙度均为 BC1，解码颜色与源相差不超过 4 |
| 缓存 | 相同设置命中缓存，设置或格式不符时重建 |
| 压缩场景渲染 | Khronos 贴图球阵 960×640 渲染正常，金属/粗糙度梯度与贴图颜色保留，无块状伪影；同参数两次运行读回逐字节相同 |
| 无贴图路径 | 固定 OBJ 场景读回与改动前逐字节一致 |

## 阶段边界

- 只实现了 BC1 与 BC3。法线的 BC5、单通道遮罩的 BC4、HDR 的 BC6H 与 BC7 尚未实现；法线贴图当前保持未压缩。
- 只支持 LDR 输入（PNG/JPEG/TGA/BMP/PSD）。HDR/EXR/KTX2/DDS 读取未接入。
- 编码器为固定端点的快速实现，未做迭代端点优化或 RDO，压缩质量低于 `bc7enc_rdo`，后续接入该库时会替换编码路径。
- 纹理资产格式变化会影响此前记录的部分读回图（贴图场景），无贴图场景不受影响。
- Windows x64、macOS arm64 与显卡实机未复验。
