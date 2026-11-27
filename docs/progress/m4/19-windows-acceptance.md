# M4-19 Windows x64 验收阶段报告

- 日期：2026-11-27
- 实现分支：`docs/m4-windows-acceptance`
- 状态：M4 Windows x64 MinGW 本地验收通过

## 本阶段交付

- 本轮不新增 M4 功能，只复验已合入 `main` 的资产管线、运行时资产与 Renderer。代码侧产出为验收报告与索引更新。
- 复验覆盖三种构建形态：Development 动态依赖、关闭资产管线的纯运行时构建、Profile 单体静态构建。

## 验证环境

Windows x64（Build 26300）、MinGW-w64 GCC 16.2.0、CMake 4.4.2、Ninja 1.13.2、libclang/Clang 23.1.6、Slang 2026.18.2、SPIRV-Tools v2026.4。Development 与纯运行时构建使用 vcpkg `x64-mingw-dynamic`，Profile 使用 `x64-mingw-static`。图形设备为 Intel RaptorLake-S Mobile Graphics Controller（驱动 101.7085），全部渲染运行都开启 Khronos Vulkan 验证层。

## 验收结果

| 验收项 | Windows x64 结果 |
| --- | --- |
| `win-dev` 完整 Development | 18/18 CTest 通过，含 asset、asset-pipeline、renderer、scene、game-module 与两项 Vulkan 专项 |
| 纯运行时构建 | `STAR_BUILD_ASSET_PIPELINE=OFF` 从干净目录配置并编译，15/15 CTest 通过；未查找 fastgltf，产物中不含资产管线库 |
| `win-profile` 单体静态 | 15/15 CTest 通过，Asset、AssetPipeline、Renderer 专项与公共头独立编译均通过 |
| 固定场景渲染 | 4 个网格部件，960×640，验证层下输出 RGBA 帧 |
| 标准数值场景 | 无贴图球阵导入 123 个部件，金属与非金属行、平滑到粗糙列表现正确，金灰两组球阵沿深度遮挡 |
| 标准贴图场景 | 贴图球阵导入 5 个部件和 2 张贴图，基础色与粗糙度变化可见 |
| 派生数据一致性 | 同一 GLB 首次导入未命中、再次导入命中；缓存载荷与序列化结果一致；直导与 DDC 载荷渲染的 PPM 逐字节相同 |
| 材质变体 | Lit 与 Unlit 变体均输出帧，两者内容不同 |
| 重复渲染 | 同一场景连续两次运行的读回图逐字节相同 |

标准场景沿用 Linux 基线资产，SHA-256 与 [数值场景](11-standard-render-scene.md)、[贴图场景](12-gltf-material-textures.md) 记录一致：`5e677f260ec0f366967a6e9545de2f3dab2a160e307b9bcb9d050cd2028f63f8` 与 `450c05557b0823a10835e32c05caf77a7b175eba94516555c1c9bb084f563f01`。派生数据载荷为 25 395 911 字节，含 5 个网格部件和 2 张贴图。

## 帧时

960×640，验证层开启，Intel RaptorLake-S 核显。

| 场景 | 部件 | 预热 | 30 帧平均 GPU 时间 |
| --- | --- | --- | --- |
| 固定 OBJ 场景 | 4 | 1.244 ms | 0.839 ms |
| 数值球阵 | 123 | 18.405 ms | 10.445 ms |
| 贴图球阵 | 5 | 8.985 ms | 4.381 ms |

同场景在 Linux llvmpipe 上的基线分别为 289.241 ms（数值球阵）和 126.785 ms（贴图球阵），硬件设备按预期大幅缩短。该数据为本机驱动基线，跨设备不可直接比较。

## 复验

构建命令见 [Windows 构建与测试指南](../../windows-build.md)。三种形态分别使用独立构建目录，Development 额外传入 `-DSTAR_TEST_WINDOWS_VULKAN=ON` 与 `STAR_SLANGC`、`STAR_SPIRV_VAL`，纯运行时构建额外传入 `-DSTAR_BUILD_ASSET_PIPELINE=OFF`。

```powershell
$build = 'build/windows-m4-dev'
$env:Path = "$(Resolve-Path "$build/vcpkg_installed/x64-mingw-dynamic/bin");$env:Path"
ctest --test-dir $build --output-on-failure

& "$build/bin/star-vulkan-forward.exe" forward-scene.obj fixed-scene.ppm 30
& "$build/bin/star-vulkan-forward.exe" MetalRoughSpheresNoTextures.glb metal-rough-values.ppm 30 --front
& "$build/bin/star-vulkan-forward.exe" MetalRoughSpheres.glb metal-rough-textured.ppm 30
```

派生数据复验用一次性校验程序完成：读取源 GLB，以 `star::GltfImporter` 元数据调用 `importCachedModel` 两次并检查命中状态，再把缓存载荷写成 `.smdl`。随后用同一可执行文件分别渲染 `.glb` 与 `.smdl`，比对读回 PPM。该校验程序位于构建目录，未提交。

## 阶段边界

- 仅验证 Intel 核显。本机 NVIDIA GeForce RTX 5060 Laptop 未被 Vulkan 加载器选中，未覆盖。MSVC 与 macOS arm64 仍未验证。
- 未重复 RenderDoc 抓帧，M2 已覆盖 Vulkan 呈现与抓帧路径。
- 本轮只做验收，未推进 M4 范围：蒙皮、动画、压缩纹理、Pak、Scene/World 桥接、异步 GPU 上传与窗口输出仍按原计划留待后续里程碑。
