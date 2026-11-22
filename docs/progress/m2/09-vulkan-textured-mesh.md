# M2-09 Vulkan 纹理网格与平台验收

- 日期：2026-11-22
- Linux x64：Vulkan 呈现、同步验证和 RenderDoc 抓帧通过。
- Windows x64：待完成 Vulkan 呈现与 RenderDoc 抓帧验收。

## 交付

- `createVulkanDevice()` 实现 Vulkan RHI 设备、VMA 缓冲区与纹理、帧边界资源回收、同步提交、资源屏障、验证消息、调试命名和 GPU 时间戳。
- CMake 在构建示例时用 Slang 生成 SPIR-V 1.6 和反射 JSON。检测到 `spirv-val` 时校验 Vulkan 1.3 输出。运行示例只读取生成的 SPIR-V。
- RenderGraph 记录 pass 间资源状态转换，并为生命周期不重叠且描述相同的瞬态纹理分配可复用槽位。示例使用编译结果分配中间颜色纹理、顺序执行绘制和复制 pass，并记录两个 pass 的 GPU 时间戳。
- 示例通过 SDL3 创建 Vulkan 窗口并呈现旋转的棋盘格网格。示例的图形管线、描述符和交换链使用 Vulkan。RHI 的公共设备接口当前覆盖资源生命周期、屏障、调试与提交。

## 构建和运行

使用可由 CMake 发现的 SDL3、Vulkan-Headers、volk 和 Vulkan Memory Allocator。在 Windows 和 Linux 上也可使用仓库固定的 vcpkg 清单。将 Slang 编译器加入 `PATH`，或配置 `-DSTAR_SLANGC=<slangc 可执行文件路径>`。构建 `StarVulkanMesh` 目标。开启验证时需要 `VK_LAYER_KHRONOS_validation`。

Linux x64：

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev --target StarVulkanMesh
build/linux-dev/bin/star-vulkan-mesh --validation --frames=120
```

Windows x64：

```powershell
cmake --preset win-dev
cmake --build --preset win-dev --target StarVulkanMesh
.\build\win-dev\bin\star-vulkan-mesh.exe --validation --frames=120
```

使用 RenderDoc 启动示例并抓取一帧。在事件浏览器中查看 `Textured Mesh` 和 `Copy to Swapchain`，在资源列表中核对 `Mesh Vertices`、`Checkerboard`、`Mesh Color` 与 `Swapchain Image`。

## Linux x64 验证

- Arch Linux，内核 6.18.64-1-lts，GCC 16.2.1。测试使用 Slang 2026.18.2、Vulkan-Headers 与 volk 1.4.363.0、VMA `0f1a757`、Vulkan 验证层 1.4.363.1 和 RenderDoc 1.46。
- `linux-dev` 完整 CTest 11 项通过。Vulkan RHI 测试在要求真实设备及验证层的模式下验证了资源创建、屏障提交、时间戳与句柄回收。
- Intel Graphics RPL-S 在 Wayland 与 X11 上均开启同步验证并成功呈现 12 帧。NVIDIA GeForce RTX 5060 Laptop GPU 在 X11 上开启同步验证并成功呈现 24 帧。RenderDoc 第 2 帧记录了两个 pass、`vkCmdDraw` 和上述资源名。
