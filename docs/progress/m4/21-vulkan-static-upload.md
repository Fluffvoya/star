# M4-21 Vulkan 静态资源上传与 Linux 复验

- 日期：2026-11-28
- 分支：`feat/m4-scene-rendering`
- 状态：Linux x64 软件 Vulkan 复验通过

## 本阶段交付

- 静态顶点、索引、点光数据、阴影常量和环境常量通过暂存缓冲复制到 GPU 优先分配的缓冲。贴图的各级 mip 和缓冲复制一起提交。
- 上传命令缓冲独立于绘制命令缓冲。Renderer 创建时提交上传，不在 CPU 上等待每次复制。首次绘制的 fence 完成后回收暂存缓冲，提前销毁时由设备空闲等待保护资源生命周期。
- 上传使用绘制所用的 Vulkan 队列。屏障明确从复制写入同步到索引、顶点和着色器读取。

## 验证结果

| 项目 | 本轮结果 |
| --- | --- |
| Linux x64 Development | 干净构建通过，16 项 CTest 全部通过 |
| 固定 OBJ 场景 | 960×640，30 帧平均 GPU 时间 4.138 ms，读回图与改动前逐字节一致 |
| Khronos 贴图球阵 | 960×640，30 帧平均 GPU 时间 128.845 ms，读回图与改动前逐字节一致 |

设备为 Intel Core i7-14650HX 上的 llvmpipe（LLVM 23.1.5）。两组离屏测试都开启 Khronos 验证层和同步验证。素材版本与 [贴图标准场景报告](12-gltf-material-textures.md) 一致。软件驱动帧时不反映独显的显存或 PCIe 性能。

## Windows 状态与阶段边界

Windows x64 的已合入基线曾在 Intel 核显及 NVIDIA 独显实测。当前会话没有原生 Windows 执行环境，本阶段改动尚待同机复验。历史结果见 [Windows 实机](19-windows-acceptance.md) 与 [独显复验](20-discrete-gpu-selection.md)。

当前上传覆盖 Renderer 初始化时的静态资源。独立 copy 队列、环形暂存、流式 mip 调度以及资产加载管理器的 GPU 就绪状态仍待接入。
