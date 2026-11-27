# M4-20 独立显卡自动选择报告

- 日期：2026-11-27
- 实现分支：`feat/auto-discrete-gpu`
- 状态：Windows x64 实机验证通过，Linux x64 与 macOS arm64 未复验

## 本阶段交付

- 新增 `star::rhi::VulkanDeviceType`，取值与 `VkPhysicalDeviceType` 对应，公共头不引入 Vulkan 头文件。
- 新增 `star::rhi::selectPreferredDevice`：按独显、核显、虚拟 GPU、其他、CPU 的顺序排序，同类型保持枚举顺序，空列表返回 `std::nullopt`。
- `VulkanRuntime::initialize` 与 `createVulkanContext` 都改用该策略。此前两者都取枚举顺序中第一个满足队列或特性要求的设备，在多 GPU 机器上可能选到核显。
- 探测路径继续手工声明 Vulkan ABI，为此补充 `vkGetPhysicalDeviceProperties` 入口，以及只读取前导字段的设备属性结构。

## 验证结果

环境同 [M4 Windows 验收](19-windows-acceptance.md)：Windows x64（Build 26300）、MinGW-w64 GCC 16.2.0、CMake 4.4.2、Ninja 1.13.2、Slang 2026.18.2、Vulkan 验证层开启。本机同时存在 Intel RaptorLake-S 核显和 NVIDIA GeForce RTX 5060 Laptop 独显。

| 验收项 | 结果 |
| --- | --- |
| 设备选择 | 改动前选中 Intel RaptorLake-S，改动后选中 NVIDIA GeForce RTX 5060 Laptop GPU |
| 选择策略单测 | 独显优先、同类型保持枚举顺序、低等级回退、空列表返回 `nullopt`，共 4 项 |
| Development 构建 | 18/18 CTest 通过 |
| 纯运行时构建 | 15/15 CTest 通过 |
| Profile 单体静态构建 | 15/15 CTest 通过 |
| 派生数据一致性 | 独显上直导 GLB 与 DDC 载荷的读回 PPM 逐字节相同 |

## 帧时

960×640，验证层开启。核显数据来自 M4 Windows 验收的同场景 30 帧记录。

| 场景 | 部件 | Intel 核显（30 帧） | NVIDIA 独显（30 帧） | NVIDIA 独显（300 帧） |
| --- | --- | --- | --- | --- |
| 固定 OBJ 场景 | 4 | 0.839 ms | 0.147 ms | 0.136 ms |
| 数值球阵 | 123 | 10.445 ms | 15.175 ms | 18.456 ms |
| 贴图球阵 | 5 | 4.381 ms | 6.811 ms | 6.376 ms |

固定场景独显快约 5.7 倍，两个球阵场景独显反而慢 1.5 倍左右。原因是样例的顶点、索引与存储缓冲通过 `MemoryType::Upload` 分配，落在 host 可见内存；独显需要跨 PCIe 读取，UMA 核显没有这项代价。该限制对应 M4 阶段边界中已登记的异步 GPU 上传待办，本轮不处理。

因此自动选独显只保证选择策略正确，不保证当前渲染路径更快。若后续需要按场景切换设备，可在选择策略之上增加显式覆盖入口，属后续工作。

## 复验

构建与测试命令见 [Windows 构建与测试指南](../../windows-build.md)。渲染命令与 M4 Windows 验收相同，样例输出的设备名即实际选中的设备。

```powershell
& "$buildDir/bin/star-vulkan-forward.exe" forward-scene.obj fixed-scene.ppm 30
```

## 阶段边界

- 本轮只改设备选择，未改渲染路径、资源驻留或上传策略。
- 选择策略为固定优先级，没有命令行或环境变量覆盖，也没有跨设备显存容量、队列能力等加权。
- 仅在 Windows x64 MinGW 与 Intel/NVIDIA 双 GPU 机器上验证。Linux x64、macOS arm64、只有单 GPU 或虚拟 GPU 的环境未复验；回退路径由单元测试覆盖，未在真实单显卡环境实测。
