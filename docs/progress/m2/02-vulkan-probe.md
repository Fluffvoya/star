# M2-02 Vulkan 加载与实例探测阶段报告

- 日期：2026-11-22
- 分支：`task/m2-phase-01-20261122`
- 状态：Linux x64 本地验证完成，Vulkan 资源后端尚未开始

## 本阶段交付

- 新增 `rhi/vulkan.hpp`，提供 Vulkan 加载错误、探测选项和探测结果类型。
- 通过 SDL3 的 Vulkan 动态加载入口获取 `vkGetInstanceProcAddr`，不要求 RHI 公共头文件暴露 Vulkan 类型。
- 探测 Vulkan 实例版本、验证层可用性、实例创建和物理设备数量。
- 在加载器、入口函数、实例创建和物理设备枚举失败时返回明确的 `std::expected` 错误。
- 将 Vulkan 探测纳入 RHI 公共头独立编译和 RHI 测试。

## 验证结果

Linux x64 `linux-dev` 预设的完整构建和 CTest 共 11 项全部通过。带 Vulkan loader 的主机返回实例版本和物理设备数量，没有 Vulkan loader 的主机允许测试返回 `LoaderUnavailable`。

## 阶段边界

本阶段没有创建逻辑设备、队列、交换链、VMA 分配器或 Vulkan 命令列表。验证层只报告 `VK_LAYER_KHRONOS_validation` 是否存在，没有启用验证回调。Windows x64 和 macOS arm64 尚未验证。
