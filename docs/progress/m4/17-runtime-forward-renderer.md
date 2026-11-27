# M4-17 独立运行时 Renderer 报告

- 日期：2026-11-27
- 分支：`feat/m4-runtime-forward-renderer`
- 状态：Linux x64 软件 Vulkan 运行时验证完成

## 本阶段交付

- 新增 `Star::Renderer`，提供离屏前向绘制、GPU 计时和 RGBA 读回 API。运行时接收 `Star::Asset` 的派生静态模型，着色器和 Vulkan 实现在 Renderer 模块内构建。
- 同一管线执行四级方向光阴影、深度预通道、GPU 分簇、贴图 PBR、IBL 和色调映射。样例负责源模型导入、材质文件解析和 PPM 输出。
- API 校验索引、贴图引用、mip 形状和材质状态。缺少 Vulkan 后端时返回可诊断错误。

## 验证结果

- Linux x64 Vulkan Development 全部 16 项 CTest 通过。没有启用 Vulkan 的 Development 构建通过 Renderer 专项和公共头独立编译。Profile 单体静态构建通过 Asset、AssetPipeline 和 Renderer 三项专项。
- 独立消费者仅链接 Renderer、Asset 和 Core，使用 Vulkan 验证层读取派生标准场景并输出 960×640 的 RGBA 帧。
- 固定场景和贴图标准场景的 PPM 与拆分前逐字节一致。Intel Core i7-14650HX、llvmpipe（LLVM 23.1.5）、Slang v2026.18.2 环境中，标准贴图场景预热 340.542 ms，后续 30 帧平均 GPU 时间 126.785 ms。
- Windows x64、macOS arm64 和硬件 GPU 尚未验证。

## 阶段边界

- 当前运行时接口支持固定尺寸离屏静态场景。Scene/World 桥接、可变尺寸与窗口输出、其他后端、异步 GPU 上传和更完整的材质状态仍待接入。
