# Windows x64 构建与测试

本指南沿用验收使用的 MinGW-w64 x64、vcpkg、Ninja 和 CMake。在仓库根目录打开 PowerShell。确认 `g++.exe`、`cmake.exe`（3.28 或更新版本）和 `ninja.exe` 可从 `PATH` 找到，`VCPKG_ROOT` 指向包含 `scripts/buildsystems/vcpkg.cmake` 的 vcpkg 安装目录。

## 配置和构建

```powershell
git submodule update --init --recursive

$buildDir = 'build/windows-dev'
$triplet = 'x64-mingw-dynamic'
$gcc = (Get-Command g++.exe).Source
$cmakeArgs = @(
    "-DCMAKE_CXX_COMPILER=$gcc"
    "-DVCPKG_TARGET_TRIPLET=$triplet"
    '-DVCPKG_HOST_TRIPLET=x64-mingw-static'
    '-DSTAR_TEST_WINDOWS_VULKAN=OFF'
)
cmake --preset win-dev -B $buildDir @cmakeArgs
cmake --build $buildDir
```

EnTT 3.14.0 和 rapidyaml 由 Git 子模块提供。Windows 预设从 `VCPKG_ROOT` 选择工具链，vcpkg 在配置时按根目录清单安装其他依赖。产物位于 `$buildDir/bin` 和 `$buildDir/lib`。编译器或 triplet 变化时，为新配置选用独立的构建目录。

| 预设 | vcpkg 目标 triplet | 运行时依赖目录 |
| --- | --- | --- |
| `win-dev` | `x64-mingw-dynamic` | `vcpkg_installed/x64-mingw-dynamic/bin` |
| `win-debug` | `x64-mingw-dynamic` | `vcpkg_installed/x64-mingw-dynamic/debug/bin` |
| `win-profile`、`win-shipping` | `x64-mingw-static` | vcpkg 依赖静态链接 |

切换预设时修改配置命令中的 `--preset`、`$buildDir` 和 `$triplet`。Debug 与 Development 使用共享 SDL3，使 Platform 和输入测试共用同一个 SDL 事件队列。

## 运行测试

在同一个 PowerShell 中，先按上表把该构建的依赖 DLL 目录加入 `PATH`。以下命令对应 Development。

```powershell
$depsBin = (Resolve-Path "$buildDir/vcpkg_installed/$triplet/bin").Path
$env:Path = "$depsBin;$env:Path"
ctest --test-dir $buildDir --output-on-failure
```

列出、筛选或重跑测试时使用以下命令。

```powershell
ctest --test-dir $buildDir -N
ctest --test-dir $buildDir -R '^core-tests$' --output-on-failure
ctest --test-dir $buildDir --rerun-failed --output-on-failure
& "$buildDir/bin/star-hello-window.exe" --frames=3
```

## 启用反射生成器测试

使用 LLVM 的 libclang 头文件、x64 导入库和对应版本的 Clang 驱动。下面假定 `clang.exe` 来自 LLVM 安装的 `bin` 目录。如果 `Get-Command` 返回的是包管理器的 shim，将 `$llvmBin` 改为实际目录。

```powershell
$llvmBin = Split-Path (Get-Command clang.exe).Source
$llvm = Split-Path $llvmBin
$env:Path = "$llvmBin;$env:Path"
$reflectionArgs = @(
    "-DSTAR_LIBCLANG_INCLUDE_DIR=$llvm/include"
    "-DSTAR_LIBCLANG_LIBRARY=$llvm/lib/libclang.lib"
    "-DSTAR_CLANG_DRIVER=$llvmBin/clang.exe"
)
cmake -S . -B $buildDir @reflectionArgs
cmake --build $buildDir
ctest --test-dir $buildDir -R '^(reflection-tests|serialization-tests|generated-reflection.*)$' --output-on-failure
```

配置找不到 libclang 时，CMake 会跳过 StarHeaderTool 及两项 `generated-reflection*` 测试。用 `ctest -N` 确认它们已注册。

## 启用 Vulkan 专项验收

在具有 Vulkan 1.3 GPU 和可用桌面的会话中运行。将 `slangc.exe` 和 `spirv-val.exe` 加入 `PATH`，并确认 Vulkan 加载器能发现 `VK_LAYER_KHRONOS_validation`。使用便携验证层时，可在当前 PowerShell 中将 `VK_LAYER_PATH` 指向其清单目录。

```powershell
$vulkanArgs = @(
    '-DSTAR_TEST_WINDOWS_VULKAN=ON'
    "-DSTAR_SLANGC=$((Get-Command slangc.exe).Source)"
    "-DSTAR_SPIRV_VAL=$((Get-Command spirv-val.exe).Source)"
)
cmake -S . -B $buildDir @vulkanArgs
cmake --build $buildDir
ctest --test-dir $buildDir -L windows-vulkan --output-on-failure
```

开启选项后，完整 CTest 也会运行 `windows-vulkan-rhi` 和 `windows-vulkan-present`。若构建生成了前向渲染样例，
`windows-vulkan-forward` 会在同步验证下渲染三帧，并将 PPM 写到构建目录的 `tests/`。Vulkan 实机结果和抓帧步骤见
[M2 Windows 验收阶段报告](progress/m2/10-windows-acceptance.md)。

## 运行 M4 渲染样例

同时启用资产管线和 Slang 的构建会生成 `bin/star-vulkan-forward.exe`。它离屏渲染静态模型并写出 PPM，默认读取构建目录中的 `forward-scene.obj`。

```powershell
& "$buildDir/bin/star-vulkan-forward.exe" forward-scene.obj fixed-scene.ppm 30
& "$buildDir/bin/star-vulkan-forward.exe" MetalRoughSpheresNoTextures.glb metal-rough-values.ppm 30 --front
```

`--material=` 指定材质文件，`.smdl` 参数按派生数据路径加载模型。标准场景来源、帧时和派生数据复验步骤见 [M4 Windows 验收阶段报告](progress/m4/19-windows-acceptance.md)。
