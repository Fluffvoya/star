# Star Engine 实现方案

> 版本：v0.2（2026-11-19）
> 读者：参与 Star Engine 开发的所有工程师
> 状态：架构基线。文中标注 **[决定]** 的条目已确认，修改需走第 19 章的决策变更流程；标注 **[建议]** 的条目是默认方案，实现负责人可以在不影响其他模块的前提下调整；标注 **[待定]** 的条目必须先讨论再动手。

---

## 目录

1. 项目目标与范围
2. 已确认的技术决策与评审意见
3. 总体架构
4. 仓库结构与构建系统
5. 编码规范与 C++23 使用约定
6. Core 基础层
7. 反射系统与代码生成器（StarHeaderTool）
8. 序列化
9. Gameplay 框架：GameObject + ECS
10. 资产系统与资产管线
11. RHI：渲染硬件接口
12. 渲染器
13. 物理
14. 音频、输入、动画
15. 游戏 UI 系统
16. 游戏模块与热重载
17. 编辑器
18. 打包与发布
19. 本地测试与质量保障
20. 里程碑与路线图
21. 风险清单
附录 A：第三方库清单
附录 B：术语表
附录 C：决策记录（ADR）索引

---

## 1. 项目目标与范围

### 1.1 目标

Star Engine 是一个用 C++23 编写的通用游戏引擎，3D 和 2D 游戏都是一等公民。它包含：

- 运行时引擎（Runtime）：游戏运行所需的全部模块。
- 编辑器（StarEditor）：场景编辑、资产管理、运行预览、打包发布。
- 工具链：反射代码生成器、资产烘焙（Cook）、打包命令行工具。

游戏代码同样使用 C++23 编写，以动态库形式被编辑器加载，支持热重载；发布时与引擎静态链接成单一可执行文件。

### 1.2 平台与图形 API

| 平台 | 编辑器 | 发布游戏 | 图形后端 |
|---|---|---|---|
| Windows 10 1909+ / 11（x64） | 支持 | 支持 | D3D12（默认）、Vulkan、D3D11 |
| Linux x64（glibc 2.35+，X11/Wayland） | 支持 | 支持 | Vulkan |
| macOS 13+（Apple Silicon / arm64） | 支持，必验 | 支持，必验 | Metal |
| macOS 13+（Intel / x86_64） | 尽力支持 | 尽力支持 | Metal |

**[决定]** Windows x64、Linux x64 和 macOS arm64 是正式支持目标，编辑器与发布游戏都必须在本机验证。Intel Mac 不阻塞发布；Windows 的 D3D11 是 1.0 兼容性目标，必须能显式选择并覆盖自动回退。

后端选择规则：

- 每个平台有默认后端，可通过命令行 `-rhi=vulkan|d3d12|d3d11|metal` 或项目设置覆盖。
- 启动时先尝试选定后端；失败后在 Windows 按 `d3d12 → vulkan → d3d11` 尝试剩余后端，不重复尝试。日志报告失败原因和最终启用的后端。后端验收必须检查最终启用的后端，不能把回退误判为成功。
- 另有 `Null` 后端，用于服务器模式和本地无 GPU 测试；不能代替正式图形后端验收。

### 1.3 明确不做的内容（第一版）

- 内置多人网络同步。**[决定]** 引擎不提供网络模块，游戏需要时自行接入第三方库。设计上保持对网络不排斥：对象有稳定 GUID，模拟逻辑可以与表现分离，但不做复制标记或 RPC。
- 移动平台、主机平台、Web 平台。架构上不阻断（RHI 和平台层都是可扩展的），但不投入。
- 脚本语言（Lua/C# 等）。gameplay 只用 C++23。
- 可视化蓝图脚本。
- GPU 物理、布料、流体模拟。

### 1.4 设计原则

1. **数据驱动的编辑器**：编辑器能看到和编辑的一切都来自反射数据，不为单个类型手写 UI，除非需要自定义绘制器。
2. **运行时与编辑器分离**：运行时模块不得依赖编辑器模块。编辑器专用代码放在 `Editor/` 或用 `STAR_WITH_EDITOR` 包裹。
3. **句柄优于指针**：跨帧保存的引用一律使用带代数（generation）的句柄或 GUID，禁止长期持有裸指针。
4. **显式的线程边界**：每个系统声明自己在哪个线程、哪个阶段运行，跨线程数据通过提取（extract）或命令队列传递。
5. **可测试**：核心模块能在没有窗口、没有 GPU 的环境下运行单元测试。

---

## 2. 已确认的技术决策与评审意见

本章列出所有已确认的决策，以及对用户最初定下的五条基础决策的评审意见。

### 2.1 决策汇总

| 领域 | 决策 | 状态 |
|---|---|---|
| Gameplay 模型 | GameObject（可继承、可写逻辑）+ ECS 组件 | [决定] |
| ECS 库 | EnTT，仅供 Scene 私有实现使用；游戏 SDK 不出现 EnTT | [决定] |
| 场景存储 | 每个场景内 GameObject 平级存储；父子关系用组件表达 | [决定] |
| 图形 API | Vulkan、D3D12、D3D11、原生 Metal | [决定] |
| RHI 基线 | 按 D3D12/Vulkan/Metal 的显式模型设计，D3D11 作为降级后端 | [决定] |
| 着色器 | Slang，一份源码编译到 SPIR-V / DXIL / DXBC / MSL | [决定] |
| 语言标准 | 引擎与游戏代码均为 C++23 | [决定] |
| C++ Modules | 第一版使用头文件 + PCH，代码组织保留迁移到 Modules 的路径 | [决定] |
| 反射 | 基于 libclang 的代码生成器（StarHeaderTool），预留迁移到 C++26 静态反射的路径 | [决定] |
| 构建系统 | 引擎、工具与游戏统一使用 CMake + Ninja；开发与发布分目录配置 | [决定] |
| 工程规范 | LLVM 格式基线，缩进与 Tab 宽度均为 4，代码注释使用英文 | [决定] |
| 质量验证 | 单人开发，允许大量使用 AI 辅助；当前以本地构建和测试为准，不搭建 CI | [决定] |
| 依赖管理 | Linux/macOS 优先系统包管理器，缺失的包用 vcpkg；Windows 全部用 vcpkg | [决定]，见 2.2.6 的补充 |
| 编辑器 UI | Dear ImGui（docking 分支） | [决定] |
| 游戏 UI | 自研 retained-mode UI | [决定] |
| 游戏代码迭代 | 游戏模块编译为动态库，编辑器内热重载；失败恢复旧模块和未保存编辑状态 | [决定] |
| 编辑期数据兼容 | 未识别的数据可往返保留；Cook 拒绝无法处理的可达内容 | [决定] |
| 3D 物理 | Jolt Physics | [决定] |
| 2D 物理 | Box2D v3 | [决定] |
| 音频 | miniaudio | [决定] |
| 编辑期资产格式 | YAML 文本（rapidyaml 解析） | [决定] |
| 发布期资产格式 | 烘焙后的二进制 + 打包文件（pak） | [决定] |
| 模型导入 | fastgltf（glTF 2.0）+ ufbx（FBX、OBJ） | [决定] |
| 网络 | 不内置 | [决定] |
| 窗口与平台抽象 | SDL3 | [建议] |
| 任务系统 | 自研 work-stealing 任务系统 | [建议] |
| 性能分析 | Tracy | [建议] |
| 骨骼动画运行时 | ozz-animation | [建议] |
| 单元测试 | doctest | [建议] |

### 2.2 对五条基础决策的评审意见

#### 2.2.1 GameObject + ECS 组合

这个组合可行，Unity（MonoBehaviour + DOTS 混合）和 UE（Actor + Mass）都走过类似的路。需要注意的地方：

1. **GameObject 不应该是"大对象"**。GameObject 只保存身份（GUID、名称、所属场景）、生命周期状态和一个 `ObjectHandle`；底层 entity 由 Scene 私有维护。位置、渲染、物理等数据都放在组件里。否则两套数据模型会互相同步，产生大量 bug。
2. **组件是纯数据，逻辑分两处**：每个对象各自的逻辑写在 GameObject 子类里（类似 UE 的 Actor）；批量处理同类数据的逻辑写在 System 里（类似 ECS 的系统）。文档第 9 章给出两者的选择准则。
3. **组件指针不稳定**。访问得到的组件指针或引用只在当前作用域借用，不得跨结构性修改或跨帧保存；需要时通过句柄重新获取（第 9.4.3 节）。
4. **迭代时禁止结构性修改**。在遍历 view 时增删组件或销毁对象，统一通过延迟命令缓冲（`SceneCommandBuffer`）在阶段末尾执行。
5. **Tick 必须可选**。几万个 GameObject 每帧都调用虚函数 `Tick` 会成为瓶颈。GameObject 默认不 Tick，需要的子类显式开启。
6. **父子关系建议用组件实现，不建议写在 GameObject 逻辑里**。原因是世界变换的传播需要按层级顺序批量计算，由系统处理比由每个对象自行处理快得多，也能避免更新顺序问题。方案见 9.6 节。

#### 2.2.2 Vulkan + D3D12 + D3D11 + Metal 四个后端

四个后端的维护成本很高。建议：

1. **实现顺序**：Vulkan → D3D12 → Metal → D3D11。Vulkan 先做是因为它能同时在 Windows 和 Linux 上验证，调试工具（RenderDoc、验证层）最完善。
2. **D3D11 为必交付的兼容后端**。保留 Basic 层级和独立验收，实现在 M10 完成；不要求与 Modern 的功能逐项相同。
3. **不要让最低端后端限制架构**。RHI 接口按显式 API 设计，D3D11 后端负责模拟（忽略屏障、用槽位绑定模拟描述符集）。bindless、光追、mesh shader 是可选能力，渲染器通过能力查询走不同路径。

#### 2.2.3 C++23

- 最低编译器版本：MSVC 19.40（Visual Studio 2022 17.10）、Clang 18、GCC 14、AppleClang 16（Xcode 16）。
- 不同标准库对 C++23 库特性的支持进度不一致，第 5.3 节给出允许使用和禁止使用的特性清单，以三个正式支持平台的本地构建和测试结果为准。
- 游戏代码也要求 C++23，项目模板的 CMake 配置会强制 `CXX_STANDARD 23`。

#### 2.2.4 编辑器

编辑器是独立可执行文件，链接全部引擎模块的动态库版本，加载游戏模块动态库。编辑器 UI 用 Dear ImGui，ImGui 的绘制通过引擎自己的 RHI 完成（不使用 ImGui 官方的各 API 后端），这样四个图形后端上的编辑器表现一致。

#### 2.2.5 Windows / Linux / macOS

- macOS 以 Apple Silicon（arm64）为默认开发、发布和验收目标。Intel Mac 尽力支持；可选输出 x86_64 或 universal，不作为发布门槛。
- Linux 同时支持 X11 和 Wayland，由 SDL3 处理。

#### 2.2.6 依赖管理：系统包管理器 + vcpkg

这个方案对日常开发很方便，但有两个问题必须在流程上解决：

1. **版本漂移**。Arch、Ubuntu、Homebrew 提供的同一个库版本可能差好几个大版本（例如 EnTT、Jolt、Slang 的 API 变化频繁）。对策：
   - 在 `cmake/Dependencies.cmake` 中为每个依赖声明最低版本和已验证的最高版本，`find_package` 带版本号，找不到合适版本时自动回退到 vcpkg。
   - API 不稳定的库（Jolt、Box2D、Slang、ImGui、rapidyaml）**始终用 vcpkg 或源码子模块**，不使用系统包。ImGui 需要 docking 分支，系统包一般不提供。EnTT 在各平台通过上游 Git 子模块固定为 3.14.0。
2. **发布产物的可移植性**。用开发机系统包构建出来的 Linux 游戏，会依赖这台机器的 glibc 和 .so 版本，放到其他发行版上可能跑不起来；Homebrew 的库不是 universal binary，也不能直接放进 .app。对策：
   - **开发构建**（Debug/Development）：按用户的方案使用系统包。
   - **发布构建**（Shipping）：所有平台统一使用 vcpkg，固定 baseline，依赖尽量静态链接（系统与后端运行库除外），仓库内固定的源码依赖随仓库构建。Linux 的发布构建在本地固定容器中进行（建议 Steam Runtime 3 "sniper" 或 Ubuntu 22.04 镜像），保证 glibc 兼容性。

---

## 3. 总体架构

### 3.1 分层

```
┌──────────────────────────────────────────────────────────────────┐
│  Applications:  StarEditor   |   GameLauncher   |   star (CLI)   │
├──────────────────────────────────────────────────────────────────┤
│  Editor 层（仅编辑器）: EditorCore, EditorUI, AssetImporters,    │
│                         ShaderCompiler, Cooker, Packager          │
├──────────────────────────────────────────────────────────────────┤
│  Gameplay 层: Scene / GameObject / Components / Systems          │
├──────────────────────────────────────────────────────────────────┤
│  引擎服务层: Renderer | Physics | Audio | Animation | UI | Input  │
├──────────────────────────────────────────────────────────────────┤
│  资源层: Asset (运行时加载) | VFS | Pak                          │
├──────────────────────────────────────────────────────────────────┤
│  RHI: RHI 接口 | Vulkan | D3D12 | D3D11 | Metal | Null           │
├──────────────────────────────────────────────────────────────────┤
│  基础层: Core | Reflection | Serialization | Platform | Jobs     │
└──────────────────────────────────────────────────────────────────┘
```

依赖规则：

- 只允许上层依赖下层，同层模块之间的依赖需要在 `docs/adr/` 中登记。
- RHI 后端模块之间互不依赖，只依赖 `RHI` 接口模块。
- 引擎服务层模块不依赖 Gameplay 层。例如 Renderer 不认识 `GameObject`，只认识 `RenderScene` 中的渲染代理（render proxy）；Gameplay 层的系统负责把组件数据同步到渲染代理。这样渲染器可以单独测试，也可以被工具（如材质预览）复用。

### 3.2 模块清单

| 模块 | 类型 | 职责 | 主要依赖 |
|---|---|---|---|
| `Core` | 运行时 | 内存、容器、字符串、日志、数学、时间、GUID、委托、断言 | mimalloc、fmt（如需）、glm |
| `Platform` | 运行时 | 窗口、输入事件、文件系统、动态库、线程、进程 | SDL3 |
| `Jobs` | 运行时 | 任务系统、并行 for、任务图 | Core |
| `Reflection` | 运行时 | 类型注册表、属性/函数/枚举元数据、类型擦除调用 | Core |
| `Serialization` | 运行时 | YAML 读写、二进制归档、版本升级 | Reflection、rapidyaml |
| `VFS` | 运行时 | 虚拟文件系统、挂载点、pak 读取、文件监视 | Platform、zstd、lz4 |
| `Asset` | 运行时 | 资产句柄、异步加载、引用计数、依赖管理 | VFS、Serialization、Jobs |
| `RHI` | 运行时 | 图形接口定义、通用工具（格式表、状态跟踪） | Core |
| `RHI.Vulkan` | 运行时 | Vulkan 后端 | volk、VMA |
| `RHI.D3D12` | 运行时 | D3D12 后端 | D3D12 Agility SDK、D3D12MA |
| `RHI.D3D11` | 运行时 | D3D11 后端 | Windows SDK |
| `RHI.Metal` | 运行时 | Metal 后端 | metal-cpp |
| `RHI.Null` | 运行时 | 空后端，用于测试和服务器 | — |
| `Renderer` | 运行时 | 渲染图、3D/2D 渲染管线、材质、光照、后处理 | RHI、Asset |
| `Physics` | 运行时 | 3D 物理（Jolt）、2D 物理（Box2D）、统一查询接口 | Jolt、Box2D |
| `Audio` | 运行时 | 音频设备、混音、3D 音效、流式播放 | miniaudio |
| `Animation` | 运行时 | 骨骼动画、动画状态机、精灵动画 | ozz-animation |
| `Input` | 运行时 | 输入动作映射、设备抽象 | Platform |
| `UI` | 运行时 | 游戏 UI（布局、样式、文字、事件、动画） | Renderer、FreeType、HarfBuzz、Yoga |
| `Scene` | 运行时 | GameObject、Scene、World、内置组件与系统 | 所需运行时模块；EnTT 为私有实现依赖 |
| `Engine` | 运行时 | 引擎主循环、模块启动与关闭、游戏模块接口 | Scene |
| `ShaderCompiler` | 编辑器/工具 | Slang 编译、变体管理、着色器缓存 | Slang |
| `AssetPipeline` | 编辑器/工具 | 导入器、派生数据缓存、烘焙 | fastgltf、ufbx、stb_image、tinyexr、bc7enc_rdo、meshoptimizer |
| `EditorCore` | 编辑器 | 编辑器框架：选择、撤销、命令、项目、PIE | Engine、AssetPipeline |
| `EditorUI` | 编辑器 | ImGui 集成、面板、属性检视器、视口 | EditorCore、Dear ImGui、ImGuizmo |
| `StarHeaderTool` | 工具 | 反射代码生成器 | libclang |
| `star` CLI | 工具 | 构建、烘焙、打包的命令行入口 | AssetPipeline、Packager |

### 3.3 构建产物形态

| 配置 | 引擎模块链接方式 | 游戏代码 | 编辑器代码 | 断言 | 优化 | 用途 |
|---|---|---|---|---|---|---|
| `Debug` | 动态库 | 动态库，可热重载 | 包含 | 全开 | -O0 | 调试引擎 |
| `Development` | 动态库 | 动态库，可热重载 | 包含 | 全开 | -O2 | 日常开发、编辑器 |
| `Profile` | 静态单体 | 静态链接 | 不包含 | 仅 `STAR_VERIFY` | -O2 + LTO | 性能分析（带 Tracy） |
| `Shipping` | 静态单体 | 静态链接 | 不包含 | 关闭 | -O2/-O3 + LTO | 发布 |

说明：

- **编辑器配置用动态库**，是为了让游戏模块能单独重新编译并热重载，同时引擎本身不必重新链接。
- **发布配置用静态单体**，好处是启动快、LTO 能跨模块优化、不需要处理 DLL 搜索路径，也避免了 Linux 上的 .so 版本问题。
- 两种形态的源代码一致，通过 `STAR_API` 宏（导出/导入/空）和 `STAR_MONOLITHIC` 宏切换。库类型由配置构建目录时选定，不在同一个 CMake target 上按构建配置切换。

### 3.4 主循环

```
Engine::Run()
  while (!quit)
    Platform::PumpEvents()           // SDL 事件 → Input
    Time::BeginFrame()
    AssetManager::ProcessCompletedLoads()   // 主线程完成异步加载的收尾
    for each World:
      World::Tick(dt)                 // 见 9.8 节的阶段划分
    UI::Update(dt)
    Renderer::Extract(worlds, ui)     // 主线程：把渲染数据复制到 RenderFrame
    RenderThread::Submit(frame)       // 交给渲染线程，最多领先 N 帧
    Audio::Update(listener)
    Time::EndFrame()                  // 帧率限制
```

线程模型：

| 线程 | 职责 |
|---|---|
| 主线程（Game Thread） | 事件处理、gameplay 逻辑、场景更新、渲染数据提取 |
| 渲染线程（Render Thread） | 执行渲染图、录制并提交命令列表、Present |
| 任务工作线程（N = 核心数 − 2，至少 2） | 并行系统、物理（Jolt 的任务）、动画采样、资产解码、命令列表并行录制 |
| IO 线程（1 个） | 文件读取、pak 解压前的原始读取 |
| 音频线程 | 由 miniaudio 管理，只做混音，不做逻辑 |

主线程和渲染线程之间默认有 1 帧延迟（主线程处理第 N+1 帧时，渲染线程在渲染第 N 帧）。GPU 侧最多 2 帧在途（frames in flight），可配置为 3。

---

## 4. 仓库结构与构建系统

### 4.1 目录结构

```
star/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json                    # vcpkg manifest（含 baseline）
├── vcpkg-configuration.json
├── cmake/
│   ├── Dependencies.cmake        # 依赖查找策略（系统包 → vcpkg 回退）
│   ├── StarModule.cmake          # star_add_module() 创建模块库并生成导出头
│   ├── StarReflection.cmake      # 接入 StarHeaderTool 的自定义命令
│   ├── StarShaders.cmake         # 引擎内置着色器的编译规则
│   └── toolchains/               # 发布构建用的工具链文件
├── Engine/
│   ├── Source/
│   │   ├── Runtime/
│   │   │   ├── Core/
│   │   │   │   ├── include/      # 对外头文件  #include "core/containers/slot_map.hpp"
│   │   │   │   ├── src/          # 实现文件和内部头文件
│   │   │   │   ├── tests/
│   │   │   │   └── CMakeLists.txt
│   │   │   ├── Platform/
│   │   │   ├── Jobs/
│   │   │   ├── Reflection/
│   │   │   ├── Serialization/
│   │   │   ├── VFS/
│   │   │   ├── Asset/
│   │   │   ├── RHI/
│   │   │   ├── RHI.Vulkan/
│   │   │   ├── RHI.D3D12/
│   │   │   ├── RHI.D3D11/
│   │   │   ├── RHI.Metal/
│   │   │   ├── RHI.Null/
│   │   │   ├── Renderer/
│   │   │   ├── Physics/
│   │   │   ├── Audio/
│   │   │   ├── Animation/
│   │   │   ├── Input/
│   │   │   ├── UI/
│   │   │   ├── Scene/
│   │   │   └── Engine/
│   │   ├── Editor/
│   │   │   ├── ShaderCompiler/
│   │   │   ├── AssetPipeline/
│   │   │   ├── EditorCore/
│   │   │   └── EditorUI/
│   │   ├── Programs/
│   │   │   ├── StarEditor/       # 编辑器 main()
│   │   │   ├── GameLauncher/     # 游戏 main()
│   │   │   ├── StarHeaderTool/
│   │   │   └── StarCLI/
│   │   └── ThirdParty/           # 只放必须源码引入的库（如 ImGui docking 的固定提交）
│   ├── Shaders/                  # 引擎内置 Slang 着色器
│   ├── Content/                  # 引擎内置资产（默认材质、图标、字体）
│   └── Config/                   # 默认引擎配置（YAML）
├── Templates/
│   ├── Blank3D/                  # 新项目模板
│   └── Blank2D/
├── Samples/                      # 示例项目，同时作为集成测试
├── Tests/                        # 跨模块集成测试、渲染图像比对测试
├── AGENTS.md                    # 代理规则索引
├── agent-rules/                  # 按需读取的代理规则
├── docs/                         # 公开文档：里程碑报告、架构基线、ADR
│   ├── architecture/implementation-plan.md
│   └── adr/                      # 架构决策记录
└── tools/                        # 本地验证脚本、格式化脚本、容器定义
```

一个游戏项目的结构（与引擎仓库分开）：

```
MyGame/
├── MyGame.starproject            # 项目描述（YAML）：名称、引擎版本、模块列表、启动场景
├── CMakeLists.txt                # find_package(StarEngine) + star_add_game_module()
├── Source/
│   └── MyGame/                   # 游戏模块源码
├── Content/                      # 源资产 + .meta 文件（提交到版本库）
├── Config/                       # 项目配置（输入映射、渲染设置等）
├── Intermediate/                 # 生成代码、派生数据缓存（不提交）
├── Saved/                        # 日志、编辑器布局、崩溃转储（不提交）
└── Build/                        # 打包输出（不提交）
```

### 4.2 CMake 约定

- **[决定]** 引擎、工具和独立游戏项目统一使用 CMake（最低 3.28）+ Ninja。使用单配置 `Ninja` 生成器，每个预设有独立 `binaryDir` 并明确 `CMAKE_BUILD_TYPE`。不维护 MSBuild 或 Xcode 构建链路。Windows 仍可使用 MSVC/clang-cl 和 Windows SDK；macOS 仍使用 AppleClang 和 Apple SDK，Metal 离线工具按需安装，均不改变 Ninja 作为构建器。
- 开发预设使用动态模块；Profile/Shipping 预设使用 `STAR_MONOLITHIC=ON` 及固定依赖。`star_add_module()` 在配置阶段选定 `SHARED` 或 `STATIC`，注册显式列出的源文件，设置公共 `include/` 和模块内部 `src/` 的搜索路径，并生成导出头。模块用 `target_link_libraries()` 声明依赖。反射生成按 §4.4 接入，本地测试使用 CTest。开发游戏通过 `StarEngineConfig.cmake` 找到匹配的引擎构建；不跨 Debug/Development 或跨 ABI 混用产物。

```cmake
star_add_module(Scene scene
    include/scene/scene.hpp
    src/scene.cpp
)
target_link_libraries(
    StarScene
    PUBLIC
        Star::Core Star::Reflection Star::Serialization Star::Asset
        Star::Renderer Star::Physics Star::Audio Star::Animation
    PRIVATE EnTT::EnTT
)
target_precompile_headers(StarScene PRIVATE src/scene_pch.hpp)
```

- `CMakePresets.json` 为 Windows x64、Linux x64、macOS arm64 提供 `win|linux|mac` 前缀的 `*-debug`、`*-dev`、`*-profile`、`*-shipping`，以及匹配的构建/测试预设；例如 `win-dev`、`linux-dev`、`mac-dev`。clang-cl、Linux 替代编译器和 Intel Mac 可提供附加预设，不代替三套正式预设。游戏模板遵循同一约定。
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` 为 IDE、静态检查提供编译数据库，但反射生成不以它为唯一参数来源（见 §4.4）。

### 4.3 依赖查找策略

`cmake/Dependencies.cmake` 中为每个依赖声明来源策略：

```cmake
star_dependency(NAME glm       VERSION 1.0      POLICY SYSTEM_THEN_VCPKG)
star_dependency(NAME SDL3      VERSION 3.2      POLICY SYSTEM_THEN_VCPKG)
star_dependency(NAME EnTT      VERSION 3.14.0   POLICY VENDORED)          # Pinned upstream submodule.
star_dependency(NAME Jolt      VERSION 5.2      POLICY VCPKG_ONLY)
star_dependency(NAME imgui     COMMIT  <hash>   POLICY VENDORED)          # Docking branch.
```

| 策略 | 含义 |
|---|---|
| `SYSTEM_THEN_VCPKG` | Linux/macOS 先 `find_package` 系统包，版本不满足或找不到时使用 vcpkg；Windows 直接 vcpkg |
| `VCPKG_ONLY` | 所有平台都使用 vcpkg，保证版本一致 |
| `VENDORED` | 源码放在 `Engine/Source/ThirdParty/`，固定提交 |

`STAR_SHIPPING_DEPS=ON`（Profile/Shipping 预设开启）时，所有 `SYSTEM_THEN_VCPKG` 都按 `VCPKG_ONLY` 处理，并使用静态库 triplet（`x64-windows-static-md`、`x64-linux`、`arm64-osx`；Intel/universal 使用附加配置）。首次安装依赖的 bootstrap 方式尚未确认；Windows 先以现有文档的 `/MD` 为发布默认，额外的 `/MT` 配置须有匹配的引擎、游戏和依赖构建（见 §18.2）。

各依赖的建议策略见附录 A。

### 4.4 生成代码与构建流程

一次完整构建的步骤：

1. CMake 配置阶段收集每个模块中带 `STAR_REFLECT` 标记的头文件列表。
2. 构建阶段对每个模块运行 StarHeaderTool（自定义命令）。CMake 直接提供头文件列表和解析参数（include、宏、目标架构、标准）；生成命令不等待业务源码成功编译。输出到 `<build>/Generated/<Module>/`：
   - 每个头文件对应一个 `<Name>.gen.hpp`，正常编译时在原头的 include 区末尾、类型声明之前包含；解析模式跳过该 include 并将 `STAR_GENERATED_BODY()` 展开为空，保证干净构建不依赖上一轮生成物。
   - 每个模块一个 `<Module>.gen.cpp`（类型注册代码）。
3. 构建规则追踪直接与传递输入、解析参数与工具版本的变化。输出内容不变则不更新时间戳；被移除的标记或头文件不得留下有效的旧注册代码。
4. 编译引擎模块和游戏模块。
5. 编译引擎内置着色器（开发配置下也可以延迟到运行时按需编译并缓存）。

---

## 5. 编码规范与 C++23 使用约定

### 5.1 命名

| 元素 | 规则 | 示例 |
|---|---|---|
| 命名空间 | 小写，引擎根命名空间 `star`，子模块 `star::rhi`、`star::physics` | `star::rhi::Device` |
| 类型、枚举、概念 | PascalCase | `GameObject`, `TextureFormat` |
| 函数、方法 | PascalCase | `CreateBuffer()`, `GetWorldTransform()` |
| 成员变量 | 类的私有成员用 `m_` + camelCase；纯数据结构体公开字段用 camelCase | `m_handle`、`castShadows` |
| 局部变量、参数 | camelCase | `frameIndex` |
| 常量、constexpr | `k` + PascalCase | `kMaxFramesInFlight` |
| 宏 | `STAR_` + 全大写 | `STAR_ASSERT` |
| 文件 | PascalCase，头文件 `.hpp`，实现 `.cpp`，Objective-C++ `.mm` | `SceneRenderer.hpp` |
| 反射生成文件 | `<原文件名>.gen.hpp` | `PlayerCharacter.gen.hpp` |

**[决定]** 仓库使用 `.clang-format` 和 `.clang-tidy`。格式基于 LLVM：`BasedOnStyle: LLVM`、`IndentWidth: 4`、`TabWidth: 4`、`UseTab: Never`、`ColumnLimit: 120`。用空格缩进；Tab 宽度为 4 不代表插入硬 Tab。代码注释和源码中的 API 文档注释使用英文，独立设计文档不受此限制。格式检查在本地执行，第三方源码和生成物不纳入手写代码的格式检查。

### 5.2 错误处理

- **引擎代码不抛异常**。可恢复错误返回 `std::expected<T, Error>`，`Error` 包含错误码和消息。编译时保留异常支持（部分第三方库需要），但异常不得跨越模块边界。
- **编程错误用断言**：
  - `STAR_ASSERT(cond, fmt, ...)`：Debug/Development 中检查，Shipping 中移除。
  - `STAR_VERIFY(cond, fmt, ...)`：所有配置中都求值表达式，Shipping 中失败只记录日志。
  - `STAR_FATAL(fmt, ...)`：不可恢复，记录日志、写崩溃转储后终止。
- **不使用 RTTI 做类型判断**。`dynamic_cast` 和 `typeid` 禁止在引擎代码中使用，改用反射系统的 `Cast<T>()` 和 `TypeOf<T>()`。RTTI 本身保持开启，以兼容第三方库。

### 5.3 C++23 特性清单

**允许使用的候选**（每项以正式支持平台上的本地工具链编译验证为准）：

| 特性 | 用途 |
|---|---|
| `std::expected` | 错误处理 |
| `std::format`、`std::print` | 日志、字符串格式化 |
| deducing `this` | 简化 CRTP 和 const/非 const 重载 |
| `if consteval` | 编译期/运行期分支 |
| `std::to_underlying` | 枚举转换 |
| `std::unreachable` | 优化提示 |
| `std::move_only_function` | 任务系统、回调 |
| 多维下标运算符 `operator[](i, j)` | 网格、图像访问 |
| `std::ranges` 的 C++23 增强（`to`、`zip`、`enumerate` 等） | 只在非热点代码中使用 |
| `std::byteswap`、`std::bit_cast` | 二进制序列化 |
| `auto(x)` decay-copy、`[[assume]]` | — |
| `std::span`、`std::mdspan` | 数据视图（`mdspan` 先确认三平台标准库可用） |

**暂不使用**：

| 特性 | 原因 |
|---|---|
| C++ Modules（`import`/`export module`） | 第一版用头文件，见 5.4 |
| `import std;` | 同上 |
| `std::generator` | 标准库支持不一致 |
| `std::stacktrace` | libc++ 支持不完整，崩溃报告用平台 API 自行实现 |
| `std::flat_map` / `std::flat_set` | 标准库支持不一致，用引擎自己的 `FlatMap` |
| 协程（用于 gameplay） | 第一版不做，后续可以作为异步加载/对话系统的扩展单独评估 |

### 5.4 预留迁移到 C++ Modules 的路径

为了以后能低成本迁移到 Modules，现在就遵守以下规则：

1. 每个模块提供一个"总入口头文件"（如 `Core/Core.hpp`），它将来会变成 `export module star.core;`。
2. 公共头文件从模块的 `include/` 路径访问，模块内部头文件留在各自的 `src/` 路径。
3. 宏只用于必要的场合（导出宏、断言、反射标记）。不在公共头文件中定义"功能性"宏，因为模块不导出宏。
4. 不依赖头文件的包含顺序，每个头文件都能单独编译（本地验证包含公共头文件独立编译）。
5. 匿名命名空间和 `static` 函数只出现在 `.cpp` 中。
6. StarHeaderTool 的输出格式不假定头文件机制，生成的注册代码放在 `.gen.cpp` 中，将来可以改成模块实现单元。

### 5.5 其他规范

- 字符串一律 UTF-8，类型为 `std::string` / `std::string_view`。与 Windows API 交互时在 Platform 层转换为 UTF-16。
- 路径使用引擎的 `Path` 类型（内部存储 `/` 分隔的 UTF-8），只在 Platform 层转换为 `std::filesystem::path`。
- 禁止在头文件中使用 `using namespace`。
- 类默认 `final`，需要被继承时去掉。gameplay 基类（`GameObject`、`Component` 基础设施）除外。
- 头文件包含顺序：对应头文件、本模块头文件、其他引擎模块、第三方库、标准库。

---

## 6. Core 基础层

### 6.1 内存

- **全局分配器**：mimalloc，通过 `STAR_ALLOCATOR=mimalloc|system` 选择。替换全局 `operator new/delete`，只在可执行文件中替换一次（游戏 DLL 和引擎 DLL 共用同一个分配器，否则跨 DLL 释放会出错）。
- **专用分配器**：
  - `LinearAllocator`：每帧重置的帧分配器，每个线程一个，用于临时数据。
  - `PoolAllocator<T>`：定长对象池。
  - `StackAllocator`：带标记回滚的栈式分配器。
  - `TrackedAllocator`：包装其他分配器，按标签（`MemTag::Renderer`、`MemTag::Physics` ...）统计内存，数据显示在编辑器的内存面板和 Tracy 中。
- 第三方库（Jolt、Box2D、EnTT、ImGui、miniaudio、Slang 等）凡是允许自定义分配器的，都接入引擎分配器并打上对应标签。

### 6.2 容器

优先使用标准库容器。引擎额外提供：

| 容器 | 用途 |
|---|---|
| `SmallVector<T, N>` | 小数据内联存储 |
| `SlotMap<T>` | 带代数的稠密存储，句柄失效可检测（GameObject 存储、RHI 资源池） |
| `FlatMap<K, V>` / `FlatSet<K>` | 有序数组实现的小型映射 |
| `HashMap<K, V>` | 开放寻址哈希表（建议基于 `ankerl::unordered_dense`，MIT） |
| `RingBuffer<T>` | 定长环形缓冲区 |
| `MPMCQueue<T>`、`SPSCQueue<T>` | 无锁队列（任务系统、渲染命令） |

### 6.3 字符串与名称

- `Name`：驻留字符串（interned string），内部是 32 位索引，比较是整数比较。用于资产类型名、属性名、骨骼名、输入动作名等高频比较的标识符。全局名称表线程安全，只增不减。
- `TypeId`、`AssetTypeId` 等基于 64 位 xxHash（XXH3）哈希的稳定 ID，跨进程、跨平台、跨构建都相同。

### 6.4 GUID

- 128 位随机 GUID（UUID v4），用于资产、GameObject、预制体实例。
- 文本形式为 32 位十六进制，在 YAML 中可读。
- 运行时查找用 `HashMap<Guid, Handle>`。

### 6.5 数学库

**[建议]** 底层使用 glm（开启 SIMD，并定义 `GLM_FORCE_DEPTH_ZERO_TO_ONE`；坐标系使用 glm 默认的右手系，见 6.5.1），引擎在 `star::math` 中提供类型别名和补充函数（`Transform`、`AABB`、`Sphere`、`Frustum`、`Ray`、`Plane`、相交测试、随机数）。理由：自研 SIMD 数学库工作量大，glm 足够成熟；以后需要时可以在别名层后面替换实现。

#### 6.5.1 坐标约定 **[决定]**

| 项 | 约定 |
|---|---|
| 坐标系 | 右手系，Y 轴向上，−Z 为前方（与 glTF 一致，减少导入转换） |
| 单位 | 1 单位 = 1 米；2D 中默认 1 单位 = 1 米，像素到单位的比例由精灵的 `PixelsPerUnit` 决定（默认 100） |
| 矩阵 | 列主序，列向量（`v' = M * v`） |
| 深度 | 0 到 1（`GLM_FORCE_DEPTH_ZERO_TO_ONE`），渲染使用反向 Z（近平面为 1） |
| 旋转 | 四元数存储，编辑器显示欧拉角（度，YXZ 顺序） |
| 纹理坐标 | 左上角为原点（D3D/Metal 习惯），Vulkan 后端通过视口 Y 翻转统一 |

### 6.6 日志

- 基于 `std::format` 自研轻量日志，不引入 spdlog（减少依赖、避免 fmt 与 std::format 并存）。
- 日志类别（`LogCategory`）带默认级别，可以在配置文件或控制台中按类别调整。
- 输出目标（sink）：控制台、文件（`Saved/Logs/`）、编辑器日志面板、平台调试输出（`OutputDebugString` / `os_log`）。
- 写日志是线程安全的，格式化在调用线程完成，写出由后台线程完成；`Fatal` 级别同步刷新。

### 6.7 委托与事件

- `Delegate<R(Args...)>`：单播，可绑定自由函数、成员函数（带对象句柄，对象销毁后自动失效）、lambda。
- `MulticastDelegate<void(Args...)>`：多播，返回 `DelegateHandle` 用于解绑。
- 绑定到 GameObject 的委托使用 `WeakRef`，GameObject 销毁后调用自动跳过，不会访问悬空指针。

### 6.8 任务系统（Jobs）

**[建议]** 自研 work-stealing 任务系统：

- 每个工作线程一个 Chase-Lev 双端队列，空闲时从其他线程偷任务。
- API：`Jobs::Run(fn)`、`Jobs::ParallelFor(range, grain, fn)`、`JobHandle::Wait()`（等待期间当前线程会执行其他任务，避免死锁和空转）。
- 任务图（`TaskGraph`）：声明任务之间的依赖，用于每帧的系统调度（第 9.8 节）。
- 任务优先级：`High`（帧内关键路径）、`Normal`、`Background`（资产解码、着色器编译）。Background 任务在单独的低优先级队列中，不阻塞帧内任务。
- 为 Jolt 实现 `JPH::JobSystem` 接口，Jolt 的任务直接跑在引擎任务系统上，避免两套线程池抢核心。
- 不使用纤程（fiber）。纤程在调试器支持、线程局部存储、第三方库兼容方面问题较多，收益不足以覆盖成本。

备选方案：enkiTS（zlib 协议）或 Taskflow（MIT）。如果自研进度落后，可以先用 enkiTS 顶上，接口保持不变。

### 6.9 平台层（Platform）

**[建议]** 使用 SDL3（zlib 协议）处理窗口、输入事件、手柄、剪贴板、文件对话框、显示器信息、高 DPI。SDL3 同时提供 `SDL_Vulkan_*` 和 `SDL_Metal_*` 辅助函数，可以直接创建 Vulkan surface 和 `CAMetalLayer`。

平台层另外自研以下部分（SDL 不覆盖或不适合）：

| 功能 | Windows | Linux | macOS |
|---|---|---|---|
| 动态库加载 | `LoadLibraryW` | `dlopen` | `dlopen` |
| 文件监视 | `ReadDirectoryChangesW` | inotify | FSEvents |
| 内存映射文件 | `CreateFileMapping` | `mmap` | `mmap` |
| 崩溃处理与转储 | SEH + MiniDumpWriteDump | 信号处理 + 自定义转储（可选接入 Breakpad/Crashpad） | 同 Linux |
| 调用栈 | DbgHelp | libunwind / `backtrace` | `backtrace` |
| 线程命名与亲和性 | `SetThreadDescription` | `pthread_setname_np` | `pthread_setname_np` |
| 高精度时钟 | `QueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` | `mach_absolute_time` |

### 6.10 配置系统

- 配置文件是 YAML，分层合并：引擎默认（`Engine/Config/`）→ 项目（`MyGame/Config/`）→ 平台（`Config/Windows/`）→ 用户本地（`Saved/Config/`）→ 命令行参数。
- 控制台变量（CVar）：`CVar<int> r_shadowQuality{"r.ShadowQuality", 2, "阴影质量 0-3", CVarFlags::Scalability}`。CVar 可以在运行时通过控制台修改，也可以写在配置文件里。

---

## 7. 反射系统与代码生成器（StarHeaderTool）

反射是编辑器、序列化、热重载、属性动画、撤销系统的共同基础，必须最先稳定。

### 7.1 需求

| 需求 | 使用方 |
|---|---|
| 枚举类型的属性（名称、类型、访问器、元数据） | 序列化、检视器、撤销、属性动画 |
| 按名字创建对象 | 场景加载、预制体、编辑器"添加组件"菜单 |
| 继承关系查询、安全向下转型 | `Cast<T>()`、检视器 |
| 枚举值与字符串互转 | 序列化、检视器下拉框 |
| 函数元数据与类型擦除调用 | 编辑器按钮（`CallInEditor`）、控制台命令、动画事件 |
| 容器属性（`std::vector`、`std::array`、`HashMap`）的统一访问 | 序列化、检视器 |
| 元数据（范围、提示、分类、显示名） | 检视器 |
| 稳定的类型 ID | 序列化文件中的类型标识、热重载后的类型匹配 |

### 7.2 标记语法

```cpp
// PlayerCharacter.hpp
#pragma once
#include "Scene/GameObject.hpp"
#if !defined(STAR_REFLECTION_PARSER)
#include "PlayerCharacter.gen.hpp"
#endif

namespace mygame {

STAR_ENUM()
enum class MoveMode : uint8_t { Walk, Run, Fly };

STAR_CLASS(Category = "Characters")
class PlayerCharacter : public star::GameObject {
    STAR_GENERATED_BODY()
public:
    STAR_PROPERTY(Edit, Range(0, 20), Tooltip = "米/秒")
    float moveSpeed = 5.0f;

    STAR_PROPERTY(Edit)
    MoveMode mode = MoveMode::Walk;

    STAR_PROPERTY(Edit, AssetType = "Prefab")
    star::AssetRef<star::Prefab> bulletPrefab;

    STAR_PROPERTY(Transient)          // Not serialized.
    int frameCounter = 0;

    STAR_FUNCTION(CallInEditor)
    void ResetStats();

protected:
    void OnBeginPlay() override;
    void OnTick(float dt) override;
};

STAR_STRUCT(Component)                // Plain ECS data component.
struct Health {
    STAR_PROPERTY(Edit, Min = 0) float current = 100.0f;
    STAR_PROPERTY(Edit, Min = 1) float max = 100.0f;
};

}
```

宏的实现方式：

- 正常编译时，`STAR_CLASS(...)`、`STAR_PROPERTY(...)` 等展开为空；`STAR_GENERATED_BODY()` 展开为 `STAR_GENERATED_BODY_<文件ID>_<行号>()`，由 `.gen.hpp` 定义，内容包括 `StaticType()` 静态函数、`GetType()` 虚函数（仅类）、友元声明（让注册代码能访问私有成员）。
- StarHeaderTool 解析时定义 `STAR_REFLECTION_PARSER`，宏展开为 `__attribute__((annotate("star:class:Category=Characters")))` 之类的注解，libclang 能在 AST 中读到这些注解和它们附着的声明。

### 7.3 StarHeaderTool 工作流程

1. **输入**：模块名、头文件列表、编译参数（include 路径、宏、`-std=c++23`、目标平台三元组）。
2. **解析**：用 libclang（`clang_parseTranslationUnit`）逐个解析头文件。为了速度：
   - 启用 `CXTranslationUnit_SkipFunctionBodies` 和 `CXTranslationUnit_Incomplete`。
   - 使用预编译头（由 StarHeaderTool 先为模块生成一个只包含公共依赖的 PCH）。
   - 多个头文件并行解析（每个线程一个 `CXIndex`）。
3. **提取**：遍历 AST，收集带注解的类、结构体、枚举、字段、函数，以及它们的基类、模板参数、默认值的文本表示、注释（作为默认 Tooltip）。
4. **校验**：
   - `STAR_CLASS` 必须包含 `STAR_GENERATED_BODY()`。
   - 属性类型必须是可反射类型（基础类型、引擎内置类型、已反射的结构体/枚举、支持的容器、`AssetRef<T>`、`ObjectRef<T>`）。
   - 报错格式与编译器一致（`file:line:col: error: ...`），IDE 可以直接跳转。
5. **输出**：`.gen.hpp`、`.gen.cpp`；只有内容变化时才写文件。
6. **缓存**：以头文件及传递输入 + 编译参数 + 工具版本为键缓存结果，存放在 `Intermediate/HeaderToolCache/`。

libclang 的来源：Windows 用 vcpkg 的 `llvm[clang]`（体积较大，建议使用本地依赖缓存）或官方 LLVM 安装包；Linux/macOS 用系统包（`libclang-dev` / Homebrew `llvm`）。StarHeaderTool 只是开发工具，不进入发布的游戏，所以它对 libclang 版本只要求 ≥ 18，不与编译引擎用的编译器绑定。

### 7.4 运行时类型注册表

```cpp
namespace star::reflect {

struct PropertyInfo {
    Name               name;
    const TypeInfo*    type;          // Property type.
    PropertyAccessOps  access;        // Generated typed accessors, erased at the API boundary.
    PropertyFlags      flags;         // Edit, Transient, ReadOnly, ...
    MetadataMap        metadata;      // Range, Tooltip, Category, DisplayName, ...
    const ContainerOps* container;    // Non-null for container properties.
};

struct TypeInfo {
    TypeId             id;            // XXH3("mygame::PlayerCharacter")
    Name               name;
    TypeKind           kind;          // Class, Struct, Enum, Primitive, Container, AssetRef, ...
    uint32_t           size, alignment;
    const TypeInfo*    base;
    std::span<const PropertyInfo> properties;
    std::span<const FunctionInfo> functions;
    std::span<const EnumEntry>    enumEntries;
    TypeOps            ops;           // Construction, destruction, copy, move, and default comparison.
    ModuleId           owner;         // Owning module for hot reload.
    uint32_t           version;       // Serialized schema version, e.g. STAR_CLASS(Version = 3).
};

class TypeRegistry {
public:
    const TypeInfo* Find(TypeId) const;
    const TypeInfo* Find(std::string_view name) const;
    std::span<const TypeInfo*> DerivedFrom(const TypeInfo* base) const;
    std::expected<void, Error> RegisterModule(ModuleId, std::span<const TypeInfo*>);
    void UnregisterModule(ModuleId);          // Used during hot reload.
    MulticastDelegate<void(ModuleId)> onTypesChanged;
};

}
```

要点：

- 类型 ID 由完全限定名哈希得到，与编译器、平台、加载顺序无关。序列化文件中存的是类型名（可读），加载时转成 ID 查找。
- 内置类型（`int32_t`、`float`、`std::string`、`Vec3`、`Quat`、`Color`、`Guid`、`Name`、`Path` ...）在 Core/Reflection 中手工注册。
- 属性读写由生成的类型擦除访问器执行；不要求多态 `GameObject` 暴露固定字节偏移。
- 容器通过 `ContainerOps` 类型擦除访问：`Size()`、`Resize()`、`GetElement(i)`、`Insert()`、`Erase()`，映射类型另有键值访问接口。
- 注册表不持有类型数据的所有权，数据在各模块的 `.gen.cpp` 中以静态存储形式存在，所以模块卸载后必须先从注册表中移除（`UnregisterModule`）。

### 7.5 C++26 静态反射迁移路径

C++26 已经纳入静态反射（P2996）。等三平台编译器都支持后，可以用编译期反射替代 libclang 生成器。为了让迁移对使用者透明：

- 标记宏的语法保持不变，迁移时只改宏的实现（例如 `STAR_PROPERTY(...)` 改为 C++26 的注解 `[[=star::Edit{}]]`）。
- 运行时的 `TypeInfo` 结构保持不变，生成方式从"外部工具生成 .cpp"改为"模板在编译期生成静态表"。
- 序列化格式只依赖类型名和属性名，与生成方式无关，不需要迁移数据。

---

## 8. 序列化

### 8.1 两种格式

| 格式 | 用途 | 实现 |
|---|---|---|
| YAML 文本 | 编辑期的场景、预制体、材质、项目配置、`.meta` 文件 | rapidyaml（MIT），反射驱动 |
| 二进制归档 | 烘焙后的资产、热重载时的状态快照、编辑器 PIE 场景复制 | 自研，反射驱动或手写 |

两种格式共享同一套 `Archive` 接口，反射驱动的序列化代码只写一遍：

```cpp
class Archive {
public:
    virtual bool IsLoading() const = 0;
    virtual void BeginObject(std::string_view key) = 0;
    virtual void EndObject() = 0;
    virtual void BeginArray(std::string_view key, size_t& count) = 0;
    virtual void EndArray() = 0;
    virtual void Value(std::string_view key, float& v) = 0;
    // ... Other primitive types.
};

void SerializeReflected(Archive&, const reflect::TypeInfo&, void* object);
```

类型可以提供自定义序列化：在类中声明 `void Serialize(Archive&)` 并在 `STAR_CLASS(CustomSerialize)` 中标记，生成器会改为调用它。

### 8.2 YAML 场景文件示例

```yaml
# Content/Levels/Forest.scene
StarFile: Scene
Version: 1
Scene:
  Name: Forest
  Settings:
    Gravity: [0, -9.81, 0]
    AmbientLight: { Color: [0.2, 0.22, 0.25, 1], Intensity: 1.0 }
GameObjects:
  - Guid: 6f1c2b7e9d0a4e3f8b5c1a2d3e4f5a6b
    Type: mygame::PlayerCharacter
    Name: Player
    Properties:
      moveSpeed: 7.5
      mode: Run
      bulletPrefab: { Asset: 3a9e0c11d2b84f6a9c7e5d4b3a2f1e0d }
    Components:
      - Type: star::Transform
        position: [0, 1, 0]
        rotation: [0, 0, 0, 1]
        scale: [1, 1, 1]
      - Type: star::Hierarchy
        parent: null
      - Type: star::MeshRenderer
        mesh: { Asset: 9b2d4c6e8f0a1b3c5d7e9f1a2b3c4d5e }
        materials: [{ Asset: 1c3e5a7b9d0f2e4a6c8b0d1f3e5a7c9b }]
      - Type: mygame::Health
        current: 80
  - Guid: a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5
    PrefabInstance:
      Prefab: { Asset: 7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b }
      Overrides:
        - Path: "Components/star::Transform/position"
          Value: [4, 0, -3]
        - Path: "Name"
          Value: "Tree_03"
```

格式规则：

- 只写与默认值不同的属性（通过 `TypeOps` 比较默认构造的对象），减少文件体积和合并冲突。场景文件里每个 GameObject 的 GUID 都是稳定的，git 合并时按对象为单位冲突。
- GameObject 按 GUID 排序写出，保证保存结果稳定，不因内存中的顺序变化而产生无意义的 diff。
- 浮点数以最短往返表示写出（`std::to_chars`），读回后与原值完全一致。

### 8.3 版本与数据升级

- 每个反射类型有 `Version`（默认 1），写文件时记录在类型条目中（只在不为 1 时写）。
- 字段重命名：`STAR_PROPERTY(Edit, FormerName = "speed")`，读取时旧名字映射到新字段。
- 类型重命名：`STAR_CLASS(FormerName = "mygame::Player")`。
- 结构性变化：类型提供升级函数 `static void Upgrade(YamlNode& node, uint32_t fromVersion)`，在反射读取之前修改原始节点。
- **[决定] 编辑期不静默丢弃无法识别的数据。** 对结构有效但暂时无法解析的类型或字段，编辑器显示诊断，并由引擎持有与模块无关的扩展数据：未知字段保存在原对象或组件下；未知组件保留类型名、版本和字段；未知 GameObject 保留 GUID、层级及原始类型和字段，使用不执行缺失逻辑的占位对象。再次保存时保留数据语义，不承诺空白和注释逐字节不变；恢复注册后的类型按前名和版本迁移规则重新绑定。
- **[决定] Cook 严格校验可达内容。** 可达场景、预制体、资产中的未知运行时类型、未知运行时字段、未能完成的迁移和缺失的必需引用均使 Cook 失败；已通过迁移显式消费的旧字段及已声明且会剥离的编辑器专用数据除外。诊断包含资产路径、对象 GUID（如适用）、类型及字段路径。不可读的文件结构或不支持的文档版本不得以空文件覆盖原文件。

### 8.4 二进制格式

- 小端序，所有平台一致（三个目标平台都是小端）。
- 烘焙资产的二进制格式以"可直接映射"为目标：头部 + 偏移表 + 数据块，加载时尽量用 `mmap`/一次读入后原地使用，避免逐字段反序列化。
- 状态快照（热重载、PIE）使用带类型名、字段名、版本和类型标签的自描述二进制格式。扩展数据在 YAML 与快照间连同值一起往返，不保存 DLL 内的对象、地址或回调。已知 `Transient` 字段不纳入快照；解析失败的字段保留原值并报告，不以默认值静默覆盖原数据。同名字段尚不能转换时只写一份，保留原值，直至迁移成功或用户显式替换。

---

## 9. Gameplay 框架：GameObject + ECS

### 9.1 概念模型

```
World（一个运行实例：编辑器世界、PIE 世界、游戏世界）
 ├── Scene "Persistent"（常驻场景，跨关卡保留的对象）
 ├── Scene "Forest"     ─┐
 └── Scene "Forest_Audio"─┴─ 可以同时加载多个场景（叠加加载）
       ├── ECS 存储               ← Scene 私有的 EnTT registry 和组件池
       ├── SlotMap<GameObject*>    ← 本场景所有 GameObject，平级存储
       ├── HashMap<Guid, Handle>   ← GUID 查找
       ├── PhysicsScene3D (Jolt)
       ├── PhysicsScene2D (Box2D)
       └── RenderScene             ← 渲染代理（由同步系统维护）
```

- **World** 持有一组 Scene、全局系统调度器、时间（可暂停、可缩放）和世界级服务（物理设置、音频监听者）。
- **Scene** 是对象容器和存储单元，对应一个 `.scene` 文件。每个场景有自己的 `entt::registry`，因此卸载场景时可以直接整体销毁注册表。
- **GameObject** 是带身份和逻辑的对象，每个 GameObject 对应其所在场景注册表中的一个 `entt::entity`。
- **Component** 是挂在 GameObject 上的纯数据结构体，经引擎 ECS 接口访问；存储细节不进入 SDK。
- **System** 是对一类组件批量执行的逻辑。

### 9.2 GameObject 存储 **[决定]**

每个场景内的 GameObject 平级存储，不在容器结构中体现父子关系。

下面是 `scene/src/` 中的实现示意。

```cpp
struct Scene::Impl {
    entt::registry                   m_registry;
    SlotMap<std::unique_ptr<GameObject>> m_objects;  // Dense storage with generational handles.
    HashMap<Guid, ObjectHandle>      m_guidLookup;   // Lookup by GUID.
    SceneCommandBuffer               m_deferred;     // Deferred structural edits.
};
```

选择说明：

- 用 `SlotMap`（线性表 + 空闲列表 + 代数）而不是单纯哈希表，是因为遍历所有对象（保存、Tick、编辑器层级面板）需要连续内存；按 GUID 的随机查找由旁边的哈希表负责。
- Scene 私有槽位保存 `ObjectHandle → entity` 映射；`entity → ObjectHandle` 使用内部 `GameObjectRef` 组件。SDK 不提供获取或构造底层 entity 的方法。

### 9.3 GameObject 类

```cpp
STAR_CLASS(Abstract = false)
class STAR_SCENE_API GameObject {
    STAR_GENERATED_BODY()
public:
    virtual ~GameObject();

    // Identity.
    const Guid&     GetGuid() const;
    const std::string& GetName() const;
    void            SetName(std::string);
    Scene&          GetScene() const;
    World&          GetWorld() const;
    ObjectHandle    GetHandle() const;

    // Borrow existing components without changing scene structure.
    template <class T> T* TryGetComponent();
    template <class T> const T* TryGetComponent() const;
    template <class T> T& GetComponent();
    template <class T> const T& GetComponent() const;
    template <class T> bool HasComponent() const;

    // Transform and hierarchy access. Parenting is deferred.
    const Transform& GetTransform() const;
    void             SetTransform(const Transform&);
    GameObject*      GetParent() const;
    void             SetParent(GameObject* parent, bool keepWorldTransform = true);
    ChildRange       GetChildren() const;

    // Lifetime controls.
    void            Destroy();                           // Queue destruction at the phase boundary.
    bool            IsPendingDestroy() const;
    void            SetActive(bool);                     // Inactive objects are skipped by ticks and systems.
    bool            IsActiveInHierarchy() const;

    // Tick controls.
    void            SetTickEnabled(bool);
    void            SetTickGroup(TickGroup);             // PrePhysics / PostPhysics / Update / LateUpdate
    void            AddTickPrerequisite(GameObject* other); // Tick after the given object.

protected:
    // Engine-driven lifecycle callbacks.
    virtual void OnCreate();          // Called after component data is restored, in editor and game.
    virtual void OnBeginPlay();       // Called when play starts, in game or PIE.
    virtual void OnTick(float dt);    // Requires SetTickEnabled(true).
    virtual void OnEndPlay(EndPlayReason);
    virtual void OnDestroy();         // Called before destruction.
    virtual void OnEnable();
    virtual void OnDisable();

#if STAR_WITH_EDITOR
    virtual void OnPropertyChanged(const reflect::PropertyInfo&); // Called after an inspector edit.
    virtual void OnEditorTick(float dt);                           // Optional tick outside PIE.
#endif

    // Collision callbacks for objects with relevant physics components.
    virtual void OnCollisionEnter(const Collision&);
    virtual void OnCollisionExit(const Collision&);
    virtual void OnTriggerEnter(GameObject& other);
    virtual void OnTriggerExit(GameObject& other);

private:
    Guid          m_guid;
    std::string   m_name;
    Scene*        m_scene;
    ObjectHandle  m_handle;
    ObjectFlags   m_flags;    // Active, PendingDestroy, BegunPlay, TickEnabled, EditorOnly, Transient ...
    TickGroup     m_tickGroup = TickGroup::Update;
};
```

设计约束：

1. GameObject 本身**不存数据性质的状态**（位置、可见性等），这些在组件中。子类可以有自己的反射属性（如 `moveSpeed`），它们属于"这个对象的逻辑参数"。
2. 子类必须可以默认构造（反射和序列化需要），初始化逻辑写在 `OnCreate` 中而不是构造函数中，因为构造时组件还没有恢复。
3. 子类可以在 `STAR_CLASS` 中声明默认组件：`STAR_CLASS(RequiredComponents = "star::Transform, star::MeshRenderer")`，创建时自动添加，编辑器中不能移除。
4. 继承深度建议不超过 3 层（`GameObject → Character → PlayerCharacter`），复用优先通过组件实现。

### 9.4 组件

#### 9.4.1 组件定义

组件是带 `STAR_STRUCT(Component)` 标记的普通结构体，不继承任何基类：

```cpp
STAR_STRUCT(Component, Category = "Rendering")
struct MeshRenderer {
    STAR_PROPERTY(Edit) AssetRef<Mesh>                  mesh;
    STAR_PROPERTY(Edit) std::vector<AssetRef<Material>> materials;
    STAR_PROPERTY(Edit) bool castShadows = true;
    STAR_PROPERTY(Edit) uint32_t layerMask = 1;
    STAR_PROPERTY(Transient) RenderProxyId proxy;       // Runtime-only data.
};
```

`Component` 标记让 StarHeaderTool 额外生成：

- 组件类型注册（在编辑器"添加组件"菜单中出现）。
- 类型擦除的组件操作表 `ComponentOps`：组件尺寸、对齐、构造、移动、析构、序列化与可选钩子。编辑器、预制体和场景加载通过 `Scene`、`ObjectHandle` 与 `TypeId` 调用引擎的加、删、查接口，不接触 ECS 后端。

#### 9.4.2 组件生命周期钩子

组件可以声明静态钩子，生成器把它们注册给 Scene；Scene 在安全点调用钩子，不把 EnTT 信号暴露给模块：

```cpp
struct RigidBody3D {
    // ...
    static void OnAdded(Scene&, ObjectHandle);    // Queue creation of the physics body.
    static void OnRemoved(Scene&, ObjectHandle);  // Queue removal of the physics body.
};
```

钩子只用于维护与外部系统的绑定（物理体、渲染代理、音频源），不写 gameplay 逻辑。

#### 9.4.3 指针稳定性

- 从 `GameObject` 或 `SystemContext` 获得的组件指针、引用只在当前调用或查询回调中借用，不能跨结构性修改、跨帧、跨线程或热重载保存。第三方系统长期持有的是引擎句柄或自己的代理，不持有组件地址。
- 跨帧引用其他对象使用 `ObjectRef<T>`（GUID + 缓存的句柄）。

#### 9.4.4 SDK 边界与跨模块组件

**[决定]** 游戏代码和 StarHeaderTool 生成的游戏代码只引用 Star SDK。EnTT 的头文件、类型、视图、存储、宏与类型标识不进入公共 API；游戏模块的 CMake 不直接依赖 EnTT。Scene 的公共头文件不包含 EnTT，即使在 Shipping 静态链接时也是如此。

- 公共模板只做类型检查、访问 Star `TypeId` 与调用引擎的类型擦除入口。组件注册时引擎检查 ID 冲突、尺寸、对齐及所有者，注册成功后由 Scene 统一在一个场景中定位组件存储。
- **[建议]** M3 先用引擎内实现的类型擦除池存放游戏组件：每池对应一个 Star `TypeId`，按 Scene 私有 entity 索引；引擎通过 `ComponentOps` 管理对齐存储和值的生命周期，不在游戏 DLL 中实例化 EnTT 池。内置组件可保留私有的 EnTT 原生存储；查询两类存储时走同一公共接口，不要求调用者知道后端。
- Scene 负责释放组件和命令缓冲里的待提交值、移除拥有模块操作表的池及缓存；所有对模块析构函数、访问器和回调的引用都必须在卸载前结束（见第 16 章）。M3 验收包含独立构建的游戏模块、无 EnTT 公共 include 的 SDK 查验、引擎与游戏访问同一组件值，以及卸载后重新注册。

### 9.5 逻辑写在哪里

| 场景 | 写法 |
|---|---|
| 一个对象独有的行为（玩家控制、Boss 逻辑、门的开关） | GameObject 子类的 `OnTick` / 事件回调 |
| 大量同类对象的统一行为（子弹移动、粒子、AI 感知、生命值回复） | System |
| 引擎功能（变换传播、渲染同步、物理同步、动画） | 引擎内置 System |
| 一次性的响应（受到伤害、拾取道具） | GameObject 方法或事件委托 |

经验规则：如果同一段逻辑会作用在 1000 个以上的对象上，写成 System。

### 9.6 父子关系与变换 **[决定：使用组件]**

#### 9.6.1 组件

```cpp
STAR_STRUCT(Component, Builtin)
struct Transform {                       // Local transform relative to the parent.
    STAR_PROPERTY(Edit) Vec3 position{0};
    STAR_PROPERTY(Edit) Quat rotation{1, 0, 0, 0};
    STAR_PROPERTY(Edit) Vec3 scale{1};
};

STAR_STRUCT(Component, Builtin, Hidden)
struct WorldTransform {                  // Derived world transform; not serialized.
    Mat4 matrix;
};

STAR_STRUCT(Component, Builtin, Hidden)
struct Hierarchy {
    STAR_PROPERTY() ObjectRef<GameObject> parent;  // Serialized parent reference.
    ObjectHandle firstChild{};                    // Runtime-only hierarchy links.
    ObjectHandle nextSibling{};
    ObjectHandle prevSibling{};
    uint32_t     childCount  = 0;
    uint16_t     depth       = 0;
    STAR_PROPERTY(EditorOnly) uint32_t siblingIndex = 0;
};

struct TransformDirty {};                // Marks a changed local transform.
```

- 只序列化 `parent` 与编辑器排序字段 `siblingIndex`，子节点链表在加载后重建。这样场景文件中父子关系只出现在一处，合并冲突少。
- 父子关系只在同一场景内有效。跨场景挂接（例如把常驻场景里的玩家挂到关卡中的载具上）通过 `AttachTo` 组件实现：不改层级，只在变换系统中跟随目标的世界变换。

#### 9.6.2 变换传播系统

1. `GetTransform()` 只读；`SetTransform()`、编辑器属性写入及通过 SDK 对 `Transform` 的可变借用均把对象标为脏。任务线程先记录脏对象，阶段边界合并，无须游戏代码接触底层存储。
2. `TransformSystem` 在阶段提交后传播脏变换，至少保证每次物理步读取前及渲染提取前已更新。Tick 表中的 `TransformPropagation` 是渲染同步前的最终检查点，不代替物理步前更新：
   - 收集所有带 `TransformDirty` 的实体，把它们的子树也标记为脏。
   - 按 `depth` 排序（同一深度内顺序无关），从浅到深计算 `WorldTransform = parent.WorldTransform * Local`。
   - 同一深度的实体可以并行计算（`ParallelFor`）。
   - 清除 `TransformDirty`。
3. 层级变化（`SetParent`）时更新 `depth`，并把整个子树标记为脏。
4. 静态对象（`Static` 标签）的世界变换在加载后只计算一次，渲染器和物理可以对它们做额外优化（静态 BVH、静态物理体）。

### 9.7 对象生命周期

```
Spawn 请求 ──► 创建 GameObject 实例（默认构造）
          ──► 在 registry 中创建 entity，添加 GameObjectRef、Transform、Hierarchy
          ──► 添加 RequiredComponents
          ──► 从模板（预制体或序列化数据）恢复属性和组件
          ──► OnCreate()
          ──► 如果世界正在运行：加入"待开始"列表，在下一个阶段边界调用 OnBeginPlay()
Destroy() ──► 标记 PendingDestroy，子对象递归标记
          ──► 阶段末尾：OnEndPlay(Destroyed) → OnDestroy() → 销毁 entity（触发组件 OnRemoved）
          ──► 从 SlotMap 中移除（句柄代数 +1，旧句柄失效）
```

- 游戏侧增删组件、创建与销毁通过 `SceneCommandBuffer`，例如 `Commands().AddComponent<T>(handle, value)`、`RemoveComponent<T>(handle)`、`Spawn<T>(params)`。创建请求返回待提交的令牌，不返回立即可访问的对象；新增组件也不返回 `T&`。引擎内部仅在未执行查询的安全点直接修改结构。
- 阶段边界依次提交结构修改、调用待开始对象的 `OnBeginPlay`、传播脏变换。提交和生命周期回调中产生的新结构命令进入下一批，不递归提交。创建后的对象或组件从下一阶段可见。`Destroy()` 仅标记销毁请求，实体与组件在提交时移除。改变父子关系也在安全点提交；`SetParent()` 不在遍历中改写层级。

### 9.8 每帧更新阶段

```
World::Tick(dt)
 ├── 1. PrePhysics       GameObject Tick（PrePhysics 组）、System（PrePhysics）
 │                        ← 输入处理、角色意图、设置物理速度
 ├── 2. Physics          固定步长循环（默认 60Hz，可配置；每帧最多 N 步，防止死亡螺旋）
 │      ├── Sync: 组件 → 物理（Kinematic 物体的位置、新增/删除的刚体）
 │      ├── Jolt Update / Box2D Step
 │      ├── Sync: 物理 → 组件（Dynamic 物体的 Transform）
 │      └── 分发碰撞与触发事件（OnCollisionEnter 等），在主线程执行
 ├── 3. PostPhysics      GameObject Tick（PostPhysics 组）、System
 ├── 4. Update           GameObject Tick（Update 组，默认组）、System（大多数 gameplay 系统）
 ├── 5. Animation        动画采样（并行）、骨骼矩阵计算、动画事件分发
 ├── 6. LateUpdate       相机跟随、IK 修正、UI 跟随世界对象
 ├── 7. TransformPropagation
 ├── 8. RenderSync       组件 → RenderScene 代理（只同步有变化的）
 └── 每个阶段结束：执行 SceneCommandBuffer（延迟的创建/销毁/组件增删），调用待开始对象的 OnBeginPlay
```

System 声明：

```cpp
STAR_CLASS()
class BulletSystem final : public star::System {
    STAR_GENERATED_BODY()
public:
    SystemDesc Describe() const override {
        return SystemDesc{}
            .Phase(TickPhase::Update)
            .Reads<Velocity>()
            .Writes<Transform>()
            .RunAfter<MovementSystem>()
            .Parallel(true);        // May run alongside non-conflicting systems.
    }
    void Execute(SystemContext& ctx) override {
        ctx.Each<const Velocity, Transform>([&](ObjectHandle, const Velocity& velocity, Transform& transform) {
            transform.position += velocity.linear * ctx.GetDeltaTime();
        });
    }
};
```

- 调度器根据每个阶段内系统声明的读写集合构建任务图，没有冲突的系统并行运行。GameObject 的 `OnTick` 在主线程上按 Tick 组和前置依赖顺序执行，不与同阶段的并行系统同时运行（GameObject 逻辑可以访问任意组件，无法声明读写集合）。
- System 由游戏模块注册，作用于 World 中的所有场景。`SystemContext::Each<const Read, Write>(fn)` 提供 Star 的类型化查询：`const` 参数对应声明的读取权限，非 `const` 参数对应写入权限；结构修改使用 `ctx.Commands()`，不能直接改底层存储。
- 编辑器模式下（不在 PIE 中）只运行标记为 `RunInEditor` 的系统（变换传播、渲染同步、动画预览等）。

### 9.9 预制体（Prefab）

- 预制体是一个资产，内容是一棵 GameObject 树（与场景文件格式相同，只是根对象唯一）。
- 实例化时生成新的 GUID，实例记录 `PrefabInstance { prefab, rootGuidInPrefab, overrides }`。
- 覆盖（override）以属性路径记录（`Components/star::Transform/position`），编辑器在检视器中把被覆盖的属性加粗显示，并提供"还原"和"应用到预制体"。
- 支持嵌套预制体（预制体中包含其他预制体的实例），GUID 通过"实例 GUID 与预制体内 GUID 组合哈希"确定性生成，保证多次加载结果一致。
- 预制体资产修改后，编辑器中所有实例自动更新（未覆盖的属性跟随变化）。

### 9.10 场景管理

- `World::LoadScene(asset, LoadMode::Single | Additive, async)`：异步加载时先在后台线程反序列化到临时结构，并等待依赖资产加载完成，再在主线程上分帧实例化（每帧最多 N 毫秒），避免卡顿。
- `World::UnloadScene(scene)`：对场景中所有对象调用 `OnEndPlay(SceneUnloaded)`，然后整体销毁 registry。
- 常驻场景（Persistent）随 World 存在，`GameObject::MoveToScene(persistent)` 可以把对象移到常驻场景（组件在 registry 之间复制）。
- 跨场景引用：`ObjectRef<T>` 按 GUID 解析，目标场景未加载时解析为空，加载后自动恢复。

### 9.11 2D 与 3D 在同一框架中

- 2D 对象使用同一个 `Transform`（`position.z` 用于排序，旋转只用绕 Z 轴的部分）。编辑器为 2D 场景提供 2D 视图模式，Transform 检视器显示简化的 2D 字段。
- 2D 专用组件：`SpriteRenderer`、`TilemapRenderer`、`RigidBody2D`、`Collider2D`（Box/Circle/Capsule/Polygon/Chain）、`SpriteAnimator`、`Light2D`（可选）。
- 一个场景可以同时包含 2D 和 3D 对象。3D 物理和 2D 物理是两个独立的世界，彼此不碰撞。
- 场景设置中的 `SceneDimension`（2D / 3D）只影响编辑器默认视图、默认相机和新建对象的默认组件，不限制运行时行为。

---

## 10. 资产系统与资产管线

### 10.1 三种资产形态

| 形态 | 位置 | 说明 |
|---|---|---|
| 源资产（Source） | `Content/**/*.png`、`*.gltf`、`*.fbx`、`*.wav`、`*.scene`、`*.mat` | 美术和设计人员编辑的文件，提交到版本库 |
| 导入后数据（Imported / Derived Data） | `Intermediate/DDC/<平台>/` | 编辑器使用的引擎内部格式，可随时重建，不提交 |
| 烘焙数据（Cooked） | `Build/<平台>/Cooked/` → 打包进 `.pak` | 发布用，针对目标平台优化 |

### 10.2 资产标识

- 每个源资产旁边有一个 `.meta` 文件（YAML），保存 GUID、导入器类型、导入设置、导入器版本：

```yaml
# Content/Characters/Hero/hero.fbx.meta
StarFile: Meta
Guid: 9b2d4c6e8f0a1b3c5d7e9f1a2b3c4d5e
Importer: star::FbxImporter
ImporterVersion: 4
Settings:
  scale: 0.01
  importAnimations: true
  generateLightmapUVs: false
  materials: CreateNew
SubAssets:                        # A source file can produce multiple sub-assets.
  - { Name: "Hero",        Type: star::Mesh,     Guid: 1f2e3d4c5b6a79881f2e3d4c5b6a7988 }
  - { Name: "Hero_Skel",   Type: star::Skeleton, Guid: ... }
  - { Name: "Run",         Type: star::AnimationClip, Guid: ... }
```

- 资产之间的引用永远是 GUID，所以在编辑器中移动、重命名文件不会破坏引用（编辑器负责同时移动 `.meta`）。
- 在编辑器外移动文件但忘了移动 `.meta`：编辑器启动扫描时，按"内容哈希 + 文件名"尝试匹配孤立的 `.meta`，匹配不到时提示用户。

### 10.3 资产类型

| 类型 | 源格式 | 运行时格式 |
|---|---|---|
| `Texture2D`、`TextureCube`、`Texture2DArray` | png、jpg、tga、bmp、psd（仅合并图层）、hdr、exr、ktx2、dds | BC1/3/4/5/6H/7 压缩，带 mip；UI 和像素风格纹理可选不压缩 |
| `Mesh`（静态/蒙皮） | gltf、glb、fbx、obj | 顶点/索引缓冲区，经过 meshoptimizer 优化，带 LOD 和 meshlet（可选） |
| `Skeleton`、`AnimationClip` | gltf、fbx | ozz-animation 运行时格式 |
| `Material`、`MaterialInstance` | `.mat`（YAML） | 参数块 + 着色器变体引用 |
| `Shader` | `.slang` | 各平台字节码 |
| `AudioClip` | wav、ogg、flac、mp3 | 短音效：解码后 PCM 或 ADPCM；长音乐：Ogg Vorbis 流式 |
| `Font` | ttf、otf | 字形表 + MSDF 图集（动态补充字形） |
| `Sprite`、`SpriteAtlas` | 纹理 + `.sprite` 切片信息 | 图集纹理 + 精灵矩形表 |
| `Tileset`、`Tilemap` | `.tileset`、`.tilemap`（YAML） | 二进制 |
| `Scene`、`Prefab` | `.scene`、`.prefab`（YAML） | 二进制场景 |
| `PhysicsMaterial` | `.physmat` | 二进制 |
| `InputActions` | `.input` | 二进制 |
| `UIDocument`、`UIStyle` | `.ui`、`.uss`（见第 15 章） | 二进制 |
| `AnimationGraph` | `.animgraph` | 二进制 |

### 10.4 运行时资产 API

```cpp
template <class T>
class AssetRef {          // Serializable GUID reference; does not retain a loaded asset.
    Guid m_guid;
public:
    AssetHandle<T> Load() const;            // Blocking load for development or loading screens.
    AssetFuture<T> LoadAsync() const;
};

template <class T>
class AssetHandle {       // Reference-counted handle that retains the loaded asset.
public:
    T*   Get() const;                        // Null until the asset is ready.
    bool IsReady() const;
    AssetState GetState() const;             // Unloaded, Loading, Ready, Failed
};
```

- **加载流程**：主线程请求 → AssetManager 查表（已加载则直接返回）→ IO 线程读取（pak 或文件）→ 工作线程解码（解压、反序列化）→ 依赖资产递归请求 → 依赖全部就绪后，在主线程或渲染线程上完成"GPU 上传"等收尾 → 状态变为 Ready。
- **引用计数**归零后资产进入"待卸载"列表，延迟若干帧（可配置）后真正释放，避免关卡切换时同一资产被反复卸载加载。
- **GPU 资源上传**通过渲染器的上传队列（copy queue）异步完成，大纹理按 mip 分批上传。
- **组件中的资产字段**用 `AssetRef<T>` 保存（可序列化），运行时由对应系统持有 `AssetHandle<T>`。场景加载时，场景引用的所有资产会作为场景的依赖一起加载完成，所以 gameplay 代码在 `OnBeginPlay` 中拿到的资产都已就绪。

### 10.5 导入器与派生数据缓存（仅编辑器）

```cpp
class AssetImporter {
public:
    virtual std::span<const std::string_view> SupportedExtensions() const = 0;
    virtual uint32_t Version() const = 0;    // Bump when importer behavior changes.
    virtual const reflect::TypeInfo& SettingsType() const = 0;
    virtual std::expected<ImportResult, Error> Import(const ImportContext&) = 0;
};
```

- 派生数据的缓存键 = 源文件内容哈希（XXH3-128）+ 导入设置哈希 + 导入器版本 + 目标平台。键不变就不重新导入。
- 导入在后台任务中进行，编辑器显示进度；导入期间资产显示占位内容（洋红色纹理、方块网格）。
- 文件监视器检测到源文件变化后自动重新导入，导入完成后在编辑器中热替换（所有持有该资产的 `AssetHandle` 指向的对象被原地更新，或者通过代数机制让持有者在下一帧重新获取）。
- **[建议]** 第二阶段提供"共享 DDC"：不同开发环境共享一个网络目录或 HTTP 缓存服务器，避免每个人都重新导入大量资产。

### 10.6 纹理管线

1. 读取：stb_image（png/jpg/tga/bmp/psd/hdr）、tinyexr（exr）、KTX-Software 或 dds 解析（已压缩的 ktx2/dds 直接使用）。
2. 处理：sRGB/线性判断（按用途：颜色贴图 sRGB，法线/粗糙度/遮罩线性）、生成 mip（Kaiser 或 Box 滤波，法线贴图 mip 重新归一化）、可选的尺寸上限、预乘 alpha。
3. 压缩（按纹理用途选格式）：

| 用途 | 格式 |
|---|---|
| 颜色（不透明） | BC1 或 BC7 |
| 颜色（带透明） | BC7（或 BC3） |
| 法线 | BC5（两通道，着色器重建 Z） |
| 单通道遮罩 | BC4 |
| HDR（天空盒、光照） | BC6H |
| UI、像素风格 | RGBA8 不压缩 |

   编码器：bc7enc_rdo（BC1-5、BC7）、ISPC Texture Compressor 或 Compressonator（BC6H）。
4. 三个桌面平台都支持 BC 格式（Apple Silicon Mac 的 Metal 支持 BC 纹理，运行时用 `supportsBCTextureCompression` 确认，不支持时回退到解压后的 RGBA8）。

### 10.7 网格管线

1. 读取：fastgltf（gltf/glb）、ufbx（fbx/obj）。导入时转换到引擎坐标系（Y 向上、米为单位）。
2. 处理：
   - 合并/拆分子网格（按材质槽）
   - 生成缺失的法线（加权平均）和切线（MikkTSpace）
   - meshoptimizer：顶点缓存优化、overdraw 优化、顶点获取优化
   - 生成 LOD（`meshopt_simplify`，默认 4 级，屏幕尺寸阈值可配置）
   - 可选：生成 meshlet（`meshopt_buildMeshlets`），供 mesh shader 或 GPU 剔除使用
   - 计算包围盒和包围球
   - 可选：生成物理碰撞体（凸包分解用 V-HACD 或 CoACD，三角网格碰撞直接用渲染网格的低 LOD）
3. 顶点格式：位置 float3；法线和切线用八面体编码压缩；UV half2（超出范围时用 float2）；蒙皮权重 4 个骨骼（uint8 索引 + unorm8 权重），超过 4 个时保留最大的 4 个并重新归一化。

### 10.8 烘焙（Cook）

- 命令：`star cook --project MyGame.starproject --platform windows --config shipping`。
- 从项目设置中的"启动场景"和"始终烘焙的资产"出发，沿依赖图收集需要的资产，没被引用的资产不进入包体。
- 每个资产按目标平台转换（纹理压缩格式、着色器字节码、去除编辑器专用数据）。
- 烘焙结果写入 `Build/<平台>/Cooked/`，同时生成资产注册表（GUID → pak 内偏移、大小、类型、依赖列表）。
- 发布前按 §8.3 校验可达内容；有未解析的必需数据时报告错误并终止，不输出部分有效的 pak。
- 烘焙是增量的，缓存键与 DDC 相同，加上烘焙器版本。

### 10.9 Pak 格式

```
PakFile
 ├── Header        魔数 "STARPAK\0"、版本、TOC 偏移、TOC 大小、标志（是否加密）
 ├── Data Blocks   每个资产一个或多个块，单独压缩（zstd 默认；需要极快解压的资产用 LZ4）
 └── TOC           资产表：GUID、类型、偏移、压缩/原始大小、压缩方式、依赖 GUID 列表、内容哈希
```

- 块大小上限 256 KB，大资产（纹理、音频）分块，支持流式读取和按 mip 读取。
- 支持多个 pak 按优先级挂载（基础包 + 补丁包 + DLC），后挂载的同 GUID 资产覆盖先前的。
- 可选的 AES-256 加密 TOC 和数据（密钥在构建时注入，只能提高提取门槛，不能防止破解）。
- 开发期运行游戏可以不打包，直接从 `Cooked/` 目录或 DDC 读取（VFS 的"松散文件"挂载点）。

---

## 11. RHI：渲染硬件接口

### 11.1 目标

- 一套接口覆盖 Vulkan、D3D12、Metal、D3D11 和 Null。
- 接口模型接近 D3D12/Vulkan/Metal（显式资源状态、命令列表、管线状态对象），D3D11 后端负责模拟。
- 渲染器以上的代码不出现任何 API 专用类型。
- 开销低：资源用句柄，热路径上不做虚函数调用以外的额外分配。

### 11.2 能力层级 **[决定]**

| 层级 | 后端与最低要求 | 能力 |
|---|---|---|
| `Basic` | D3D11 FL 11_0 | 槽位绑定、无异步计算、无 bindless、无光追、无 mesh shader |
| `Modern` | Vulkan 1.3（必需扩展见下）、D3D12 FL 12_0 + SM 6.6、Metal 3（macOS 13+） | bindless、异步计算队列、间接绘制计数、timeline 同步 |
| 可选能力 | 按设备查询 | 光线追踪（`VK_KHR_ray_query` / DXR 1.1 / Metal RT）、mesh shader、VRS、64 位原子 |

渲染器在启动时调用 `Device::GetCapabilities()`，根据层级选择渲染路径。同一渲染功能在 `Basic` 层级可以降级或关闭（例如 GPU 驱动剔除降级为 CPU 剔除）。

Vulkan 必需特性：dynamic rendering、synchronization2、descriptor indexing（含 `runtimeDescriptorArray`、`descriptorBindingPartiallyBound`、`descriptorBindingUpdateAfterBind`）、timeline semaphore、buffer device address。这些在 Vulkan 1.3 中都已是核心或广泛支持。

### 11.3 核心对象

| 对象 | 说明 | Vulkan | D3D12 | Metal | D3D11 |
|---|---|---|---|---|---|
| `Device` | 设备、资源工厂、能力查询 | VkDevice | ID3D12Device | MTLDevice | ID3D11Device |
| `Queue` | Graphics / Compute / Copy | VkQueue | ID3D12CommandQueue | MTLCommandQueue | 立即上下文（只有一个） |
| `CommandList` | 命令录制 | VkCommandBuffer | ID3D12GraphicsCommandList7 | MTLCommandBuffer + 编码器 | 延迟上下文或直接录制到自有命令流 |
| `Buffer` | 顶点、索引、常量、存储、间接参数 | VkBuffer + VMA | ID3D12Resource + D3D12MA | MTLBuffer | ID3D11Buffer |
| `Texture` | 1D/2D/3D/Cube/Array，可做 RT/DS/UAV | VkImage | ID3D12Resource | MTLTexture | ID3D11Texture* |
| `TextureView` | 子资源视图 | VkImageView | 描述符 | MTLTexture 视图 | SRV/RTV/DSV/UAV |
| `Sampler` | 采样器 | VkSampler | 采样器描述符 | MTLSamplerState | ID3D11SamplerState |
| `ShaderModule` | 编译好的着色器字节码 | VkShaderModule | DXIL blob | MTLLibrary + MTLFunction | ID3D11*Shader（DXBC） |
| `PipelineLayout` | 绑定布局 | VkPipelineLayout | Root Signature | — | — |
| `GraphicsPipeline` / `ComputePipeline` | 管线状态 | VkPipeline | PSO | MTLRenderPipelineState + MTLDepthStencilState | 状态对象组合 |
| `BindGroup` | 一组资源绑定 | VkDescriptorSet | 描述符表 | 参数缓冲区 | 槽位表 |
| `Fence` | CPU/GPU 同步（timeline 语义） | timeline semaphore | ID3D12Fence | MTLSharedEvent | ID3D11Query（模拟） |
| `Swapchain` | 交换链 | VkSwapchainKHR | IDXGISwapChain4 | CAMetalLayer | IDXGISwapChain |
| `QueryPool` | 时间戳、遮挡查询 | VkQueryPool | ID3D12QueryHeap | MTLCounterSampleBuffer | ID3D11Query |

### 11.4 接口风格

```cpp
namespace star::rhi {

struct BufferDesc {
    uint64_t     size;
    BufferUsage  usage;          // Vertex | Index | Constant | Storage | Indirect | CopySrc | CopyDst
    MemoryType   memory;         // GpuOnly, Upload, Readback
    const char*  debugName;
};

class Device {
public:
    virtual ~Device() = default;
    virtual const Capabilities& GetCapabilities() const = 0;

    virtual BufferHandle   CreateBuffer(const BufferDesc&, const void* initialData = nullptr) = 0;
    virtual TextureHandle  CreateTexture(const TextureDesc&) = 0;
    virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    // ...
    virtual void Destroy(BufferHandle) = 0;   // Release only after GPU use completes.

    virtual CommandList* BeginCommandList(QueueType) = 0;
    virtual SubmitToken  Submit(QueueType, std::span<CommandList* const>, std::span<const SyncPoint> waits) = 0;
    virtual void         Present(SwapchainHandle) = 0;

    virtual void BeginFrame() = 0;            // Reclaim completed resources and advance the frame index.
    virtual void EndFrame() = 0;
};

class CommandList {
public:
    virtual void Barrier(std::span<const BufferBarrier>, std::span<const TextureBarrier>) = 0;
    virtual void BeginRenderPass(const RenderPassDesc&) = 0;
    virtual void EndRenderPass() = 0;
    virtual void SetPipeline(PipelineHandle) = 0;
    virtual void SetBindGroup(uint32_t slot, BindGroupHandle) = 0;
    virtual void PushConstants(const void* data, uint32_t size) = 0;
    virtual void SetVertexBuffers(...) = 0;
    virtual void SetIndexBuffer(...) = 0;
    virtual void Draw(...) = 0;
    virtual void DrawIndexed(...) = 0;
    virtual void DrawIndexedIndirect(...) = 0;
    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void CopyBuffer(...) = 0;
    virtual void CopyBufferToTexture(...) = 0;
    virtual void BeginDebugMarker(const char*, Color) = 0;
    virtual void EndDebugMarker() = 0;
    virtual void WriteTimestamp(QueryPoolHandle, uint32_t index) = 0;
};

}
```

- **句柄**：`BufferHandle` 等是 32 位池索引 + 代数，资源对象存放在后端的 `SlotMap` 中。
- **延迟销毁**：`Destroy` 把资源放入当前帧的销毁队列，在该帧的 GPU 工作完成后（通过 fence 判断）真正释放。
- **线程**：`Device` 的资源创建接口线程安全；每个 `CommandList` 同一时刻只能被一个线程录制；多个线程可以并行录制不同的命令列表。
- **调试名**：所有资源都要求提供 `debugName`，后端设置到 API 对象上（`vkSetDebugUtilsObjectNameEXT`、`SetName`、`label`），RenderDoc/PIX/Xcode 中可见。

### 11.5 资源状态与屏障

- 接口采用"增强屏障"风格（与 Vulkan synchronization2 和 D3D12 Enhanced Barriers 一致）：每个屏障声明 `before/after` 的访问类型（`ShaderRead`、`RenderTarget`、`DepthWrite`、`CopyDst` ...）、同步阶段和纹理布局。
- 渲染器层面通过**渲染图**自动计算屏障（第 12.2 节），绝大多数渲染代码不手写屏障。
- 各后端处理：
  - Vulkan：映射为 `vkCmdPipelineBarrier2`。
  - D3D12：设备支持 Enhanced Barriers 时直接映射，否则转为传统的 `ResourceBarrier`（状态转换）。
  - Metal：Metal 自动跟踪资源（`MTLHazardTrackingModeTracked`）时忽略大部分屏障；对于为性能设为 `Untracked` 的资源（如 bindless 堆中的资源），转换为 `MTLFence` / `memoryBarrier`。
  - D3D11：忽略屏障，只在必要时解除绑定（例如把纹理从 RTV 切到 SRV 前解除 RTV 绑定）。

### 11.6 绑定模型

这是四个后端差异最大的部分，采用"固定频率分组 + 可选 bindless"的方案：

| 绑定组槽位 | 内容 | 更新频率 |
|---|---|---|
| 0 | Frame：时间、全局采样器、全局光照数据 | 每帧 |
| 1 | View：相机矩阵、视图相关的纹理（阴影图、深度金字塔） | 每视图 |
| 2 | Material：材质参数常量缓冲区和纹理 | 每材质 |
| 3 | Draw/Pass：pass 专用资源 | 每 pass 或每次绘制 |
| Push constants | 绘制 ID、少量参数（≤ 128 字节） | 每次绘制 |

`Modern` 层级额外启用 bindless：

- 所有纹理和缓冲区放进一个全局描述符堆（Vulkan descriptor indexing 的大数组 / D3D12 SM 6.6 的 `ResourceDescriptorHeap` / Metal argument buffer tier 2）。
- 材质参数里存的是纹理索引而不是纹理绑定，绘制时只需要 push constants 里的实例/材质索引，这样可以做 GPU 驱动渲染和大批量合批。

`Basic` 层级（D3D11）：

- 绑定组展开为具体的 `VSSetShaderResources` / `PSSetConstantBuffers` 等槽位调用，槽位分配由着色器编译器的反射数据决定。
- Push constants 用一个每次绘制更新的小常量缓冲区模拟。

Slang 着色器通过 `ParameterBlock<T>` 声明每个绑定组，Slang 为每个目标生成对应的布局（Vulkan descriptor set、D3D12 root signature 描述、Metal argument buffer、D3D11 寄存器），着色器编译器把布局信息写入反射数据，RHI 用这些数据创建 `PipelineLayout`。

### 11.7 各后端实现要点

#### Vulkan

- 加载器：volk（直接加载函数指针，减少分发开销）。
- 内存：Vulkan Memory Allocator（VMA）。
- 每帧每线程一个 `VkCommandPool`，帧结束后整体重置。
- 管线缓存：`VkPipelineCache` 持久化到 `Saved/ShaderCache/`，按驱动版本和设备 ID 区分。
- 验证层在 Debug/Development 中默认开启（可关闭），验证消息转发到引擎日志并在出错时断言。
- 呈现：优先 `MAILBOX`（关闭垂直同步时）或 `FIFO`（开启时）。Linux 上同时测试 X11 和 Wayland surface。

#### D3D12

- 使用 D3D12 Agility SDK（随游戏分发 `D3D12Core.dll`），以获得 SM 6.6 和 Enhanced Barriers。
- 内存：D3D12 Memory Allocator（D3D12MA）。
- 描述符：一个大的 shader-visible CBV/SRV/UAV 堆（100 万个）和采样器堆（2048 个），按帧环形分配临时描述符，持久描述符用空闲列表。
- 开启 DRED（Device Removed Extended Data）以便诊断 GPU 崩溃。
- Debug 层和 GPU-based validation 可通过命令行开启。

#### Metal

- 使用 metal-cpp（Apple 官方的 C++ 封装，Apache 2.0），保持后端主体是 C++。与窗口系统交互的少量代码（`CAMetalLayer`、`NSAutoreleasePool`）放在 `.mm` 文件中。
- Metal 3 起，argument buffer 可以直接写入 GPU 地址和资源 ID，用于 bindless。
- bindless 堆中的资源需要在每个编码器中调用 `useResources` / `useHeap`，否则 GPU 访问不到；建议把 bindless 资源放在 `MTLHeap` 中，每个编码器调用一次 `useHeap`。
- 命令列表映射：一个 `CommandList` 对应一个 `MTLCommandBuffer`，`BeginRenderPass` 创建 `MTLRenderCommandEncoder`，渲染 pass 外的拷贝和计算命令按需创建 `MTLBlitCommandEncoder` / `MTLComputeCommandEncoder`。
- 管线状态：Metal 把深度模板状态与渲染管线分开，后端在 `GraphicsPipeline` 中同时保存两者。
- 用 `MTLBinaryArchive` 缓存管线编译结果。
- 调试：Xcode GPU 帧捕获、Metal API Validation、Shader Validation。

#### D3D11

- 使用 `ID3D11Device5` / `ID3D11DeviceContext4`（Windows 10 自带）。
- 着色器使用 DXBC（Slang 通过 FXC 生成，SM 5.0）。
- 多线程录制：每个 `CommandList` 录制到引擎自己的轻量命令流（不使用 D3D11 延迟上下文，它的性能在很多驱动上不理想），提交时由渲染线程在立即上下文上回放。
- 屏障和 fence 基本是空操作或用 Query 模拟。

#### Null

- 所有创建调用返回有效句柄，所有命令是空操作。用于单元测试、服务器模式和本地无 GPU 测试。

### 11.8 GPU 调试与分析

- 所有后端支持调试标记（嵌套的 `BeginDebugMarker`），标记与渲染图的 pass 名称一致。
- 每个渲染 pass 写入 GPU 时间戳，在 Tracy 和编辑器的 GPU 分析面板中显示。
- Windows：PIX、RenderDoc、Nsight；Linux：RenderDoc、Nsight；macOS：Xcode。编辑器提供"捕获下一帧"按钮，按当前后端调用 RenderDoc API 或 `MTLCaptureManager`。

---

## 12. 渲染器

### 12.1 结构

```
Renderer
 ├── RenderScene        渲染代理集合（MeshProxy、LightProxy、DecalProxy、SpriteProxy ...），由 RenderSync 系统维护
 ├── RenderFrame        每帧从主线程提取的不可变快照（视图、可见代理列表、UI 绘制数据、调试绘制）
 ├── RenderGraph        每帧构建的 pass 依赖图
 ├── Pipelines
 │    ├── ForwardPlusPipeline   3D 默认管线
 │    └── Pipeline2D            2D 管线
 ├── MaterialSystem     材质、着色器变体、PSO 缓存
 ├── GpuScene           Modern 层级：GPU 上的实例/材质/变换缓冲区
 ├── UploadManager      异步上传（copy queue + 环形暂存缓冲区）
 └── DebugDraw          线、框、球、文字（编辑器和调试用）
```

渲染数据流：

1. 主线程 `RenderSync` 系统把组件变化写入 `RenderScene`（增量：新增/删除/变换变化/材质变化）。
2. 主线程 `Renderer::Extract`：对每个相机做视锥剔除（CPU 层面，按 BVH），生成 `RenderFrame`。
3. 渲染线程拿到 `RenderFrame` 后构建渲染图、执行、提交。主线程此时已在处理下一帧，不能修改该帧的数据。

`RenderScene` 采用双缓冲或"写入命令队列"方式，保证主线程写入和渲染线程读取不冲突。**[建议]** 使用命令队列：主线程把变化记录成命令，渲染线程在每帧开始时应用。

### 12.2 渲染图（Render Graph）

```cpp
graph.AddPass("GBuffer/DepthPrepass", PassType::Raster,
    [&](PassBuilder& b) {
        depth = b.CreateTexture("SceneDepth", {.format = Format::D32_FLOAT, .size = viewSize});
        b.WriteDepth(depth);
    },
    [=](PassContext& ctx) {
        DrawOpaqueDepthOnly(ctx.cmd, ctx.frame);
    });
```

功能：

- **声明式资源**：pass 声明读写哪些纹理/缓冲区，图编译时计算屏障、资源生命周期。
- **瞬态资源复用**：生命周期不重叠的瞬态纹理共享内存（Vulkan/D3D12/Metal 用 aliasing，D3D11 退化为按描述复用纹理对象）。
- **剔除**：没有被最终输出使用的 pass 自动跳过。
- **异步计算**：pass 可以标记 `AsyncCompute`，在 `Modern` 层级上调度到计算队列并自动插入跨队列同步；`Basic` 层级上退回图形队列。
- **并行录制**：编译后按 pass 拆分成若干批次，在工作线程上并行录制命令列表。
- **可视化**：编辑器中可以查看本帧的渲染图（pass、资源、生命周期），并预览任意中间纹理。

### 12.3 3D 渲染管线 **[建议：Clustered Forward+]**

选择 Clustered Forward+ 而不是延迟渲染作为默认管线的原因：

- 支持 MSAA，透明物体和不透明物体用同一套光照。
- 材质模型灵活（不受 G-Buffer 布局限制），自定义着色模型容易加。
- 在 D3D11 和带宽有限的集显、Apple Silicon（TBDR 架构）上表现好。
- 延迟渲染需要的屏幕空间效果（SSAO、SSR）可以通过深度预通道 + 薄 G-Buffer（法线 + 粗糙度）实现。

管线步骤：

| 步骤 | 说明 | 层级 |
|---|---|---|
| 1. GPU 剔除 | 实例视锥剔除 + 基于上一帧深度金字塔的遮挡剔除，生成间接绘制参数 | Modern（Basic 用 CPU 剔除） |
| 2. 深度预通道 | 不透明物体写深度，同时输出法线/粗糙度（薄 G-Buffer）和运动向量 | 全部 |
| 3. 深度金字塔 | Hi-Z，用于遮挡剔除和屏幕空间效果 | 全部 |
| 4. 光源分簇 | 视锥按 16×9×24 切分为簇，计算着色器把光源分配到簇 | 全部 |
| 5. 阴影 | 方向光：级联阴影（4 级，PCF 或 PCSS）；点光/聚光：阴影图集，按屏幕占比分配分辨率 | 全部 |
| 6. SSAO | GTAO，半分辨率 + 双边上采样 | 全部 |
| 7. 不透明着色 | 前向着色，读取簇光源列表、阴影、AO、IBL | 全部 |
| 8. 天空 | 天空盒 / 程序化大气（Hillaire 2020 方法） | 全部 |
| 9. 透明 | 排序后前向着色，支持折射（采样不透明场景颜色副本） | 全部 |
| 10. 粒子 | GPU 粒子（Modern）/ CPU 粒子（Basic） | 全部 |
| 11. 后处理 | TAA → 景深（可选）→ 运动模糊（可选）→ Bloom → 自动曝光 → 色调映射（AgX 默认，ACES 可选）→ 颜色分级（LUT）→ FXAA（未开 TAA 时）→ 锐化 | 全部 |
| 12. UI | 游戏 UI 绘制到最终图像 | 全部 |

光照模型：

- PBR 金属度/粗糙度工作流（与 glTF 2.0 一致），GGX 镜面 + Lambert 漫反射（可升级到多次散射近似）。
- 光源类型：方向光、点光、聚光、矩形面光（LTC，第二阶段）。
- 间接光照：基于图像的光照（IBL：预滤波的环境贴图 + 球谐漫反射），反射探针（盒投影），光照探针（球谐，烘焙）。
- 第二阶段：光照贴图烘焙（GPU 路径追踪烘焙器，用于静态物体）；`Modern` + 光追能力下的光追阴影和反射。

渲染设置通过可伸缩性（Scalability）预设暴露：低/中/高/极高，每个预设对应一组 CVar。

### 12.4 2D 渲染管线

| 功能 | 说明 |
|---|---|
| 精灵批处理 | 按（排序层、层内顺序、材质、纹理）排序后合批；Modern 层级用 bindless 纹理，不同纹理也能合批 |
| 排序 | 排序层（Sorting Layer）+ 层内顺序（Order in Layer）+ 可选按 Y 排序（俯视角游戏） |
| 瓦片地图 | 按区块（chunk，32×32 瓦片）生成静态网格，只重建变化的区块；支持动画瓦片 |
| 九宫格精灵 | 用于 2D 场景中的可拉伸元素 |
| 2D 光照（可选） | 法线贴图精灵 + 2D 点光/全局光 + 阴影投射体，渲染到光照缓冲区再合成 |
| 像素完美相机 | 按整数倍缩放渲染到低分辨率目标再放大，避免像素抖动 |
| 后处理 | 复用 3D 管线中的 Bloom、颜色分级等 |

3D 和 2D 混合：一个相机组件指定管线类型（3D/2D）和渲染层掩码。多个相机按 `depth` 顺序渲染到同一个目标（例如 3D 场景上叠加 2D 前景），或渲染到渲染目标纹理（小地图、画中画）。

### 12.5 材质系统

- **着色器**（`.slang`）定义材质的表面函数，引擎提供标准表面模板（以下为示意语法，属性标记的具体写法由着色器框架实现时确定）：

```slang
// Content/Shaders/MyLit.slang
import star.material;

[MaterialParams]
struct Params {
    float4 baseColor;          [Default(1,1,1,1)] [Color]
    Texture2D albedo;          [Default("white")]
    float roughness;           [Default(0.5)] [Range(0, 1)]
};

SurfaceOutput Surface(MaterialInput input, Params p) {
    SurfaceOutput o;
    o.baseColor = p.albedo.Sample(star.linearSampler, input.uv) * p.baseColor;
    o.roughness = p.roughness;
    return o;
}
```

- **材质**（`.mat`）引用一个着色器，并给出参数值；**材质实例**引用一个材质，只覆盖部分参数。
- 材质参数的反射信息来自 Slang 的反射 API，编辑器据此生成材质检视器。
- 渲染状态（混合模式、双面、深度测试、着色模型）作为材质属性，决定着色器变体和 PSO。
- 第二阶段：节点式材质编辑器，生成 Slang 代码（使用 imnodes 或 ImGui Node Editor 实现界面）。

### 12.6 着色器编译与变体管理

- **编译器**：Slang（MIT），编辑器和烘焙器中链接，运行时（发布版）不包含。
- **目标**：

| 后端 | 输出 | 说明 |
|---|---|---|
| Vulkan | SPIR-V 1.6 | — |
| D3D12 | DXIL（SM 6.6） | Slang 调用 DXC，需要 `dxcompiler.dll` 和 `dxil.dll`（Windows 上随编辑器分发） |
| D3D11 | DXBC（SM 5.0） | Slang 调用 FXC（`d3dcompiler_47.dll`），只能在 Windows 上编译 |
| Metal | MSL 源码 → 离线编译为 `.metallib` | Slang 输出 MSL，`metal`/`metallib` 命令行工具只在 macOS 上可用 |

- **跨平台烘焙的限制**：DXBC 只能在 Windows 上生成，`.metallib` 只能在 macOS 上生成。对策：
  - 烘焙 Metal 时如果不在 macOS 上，打包 MSL 源码，首次运行时用 `newLibraryWithSource` 编译并缓存（慢，只作为开发期的应急方案）。
  - 正式发布：在对应本地宿主上烘焙并验证；Windows 生成 DXBC，macOS 生成 `.metallib`，不把开发期 MSL 源码回退当作发布通路。
- **变体**：
  - 着色器中用 `[Feature("NORMAL_MAP")]` 之类的标记声明变体开关，引擎内部开关（蒙皮、实例化、阴影 pass、2D/3D）由管线自动设置。
  - 烘焙时只编译实际用到的组合（根据材质和网格类型推导），并记录到变体清单。
  - 运行时遇到清单外的变体：开发期即时编译；发布版回退到最接近的已编译变体并记录警告。
  - Slang 的 specialization constants / link-time specialization 可以减少一部分变体数量，优先使用。
- **缓存**：
  - 编辑器中按（源文件哈希 + 包含文件哈希 + 变体键 + 目标 + 编译器版本）缓存字节码到 DDC。
  - 着色器源文件修改后自动重新编译并热替换 PSO。
- **PSO 预热**：发布版在加载界面中根据变体清单预创建 PSO（D3D12/Vulkan 的管线缓存、Metal 的 `MTLBinaryArchive`），避免游戏中首次绘制时卡顿。

### 12.7 其他渲染功能清单

| 功能 | 阶段 |
|---|---|
| 骨骼蒙皮（计算着色器预蒙皮，结果复用于所有 pass） | 第一阶段 |
| 网格 LOD 选择、LOD 渐变 | 第一阶段 |
| 贴花（延迟贴花，写入薄 G-Buffer 前） | 第二阶段 |
| 地形（高度图 + CDLOD/clipmap，分层材质） | 第二阶段 |
| 植被实例化（GPU 实例化 + GPU 剔除） | 第二阶段 |
| 体积雾 | 第二阶段 |
| 屏幕空间反射 | 第二阶段 |
| 光照贴图烘焙 | 第二阶段 |
| 光追阴影/反射/AO | 第三阶段（仅 Modern + RT） |
| 虚拟纹理、虚拟几何 | 不在计划内 |

---

## 13. 物理

### 13.1 总体 **[决定：3D 用 Jolt，2D 用 Box2D v3]**

```
Physics 模块
 ├── physics3d::PhysicsScene   封装 JPH::PhysicsSystem
 ├── physics2d::PhysicsScene   封装 Box2D 的 b2WorldId
 ├── 统一的查询接口（射线、形状投射、重叠），3D 和 2D 各一套
 └── Scene 模块中的同步系统和组件
```

选型依据（详见附录 C 的 ADR-0007）：在常见游戏场景中 Jolt 与 PhysX 性能处于同一量级，Jolt 多核扩展性更好、原生支持 macOS（含 Apple Silicon）、支持跨平台确定性和双精度，许可证为 MIT。

Physics 模块不依赖 GameObject，只接收"物体描述 + 引擎侧的不透明用户标识"，这样物理可以单独测试，以后换物理引擎时只影响 Physics 模块和同步系统。

### 13.2 3D 组件

| 组件 | 说明 |
|---|---|
| `RigidBody3D` | 运动类型（Static / Kinematic / Dynamic）、质量、阻尼、重力系数、运动质量（Discrete / LinearCast，即 CCD）、锁定轴、是否允许休眠、碰撞层 |
| `BoxCollider3D`、`SphereCollider3D`、`CapsuleCollider3D`、`CylinderCollider3D` | 基础形状，带本地偏移和旋转 |
| `ConvexCollider3D` | 凸包（来自网格资产，导入时预计算） |
| `MeshCollider3D` | 三角网格，只能用于 Static 或 Kinematic |
| `HeightFieldCollider3D` | 地形 |
| `CompoundCollider` | 同一对象上的多个碰撞体自动合并为 Jolt 的 `StaticCompoundShape`/`MutableCompoundShape`；子对象上的碰撞体（没有自己的刚体时）也合并到最近的父刚体 |
| `CharacterController3D` | 基于 `JPH::CharacterVirtual`：台阶、坡度限制、贴地、推动刚体 |
| `Joint3D` 系列 | Fixed、Hinge、Slider、Distance、Cone、SwingTwist、SixDOF，引用另一个刚体的 `ObjectRef` |
| `Vehicle3D`（第二阶段） | 基于 Jolt 的轮式车辆 |
| `Ragdoll`（第二阶段） | 基于骨骼资产生成的布娃娃，与动画系统混合 |
| `SoftBody3D`（第三阶段） | Jolt 软体 |

### 13.3 2D 组件

| 组件 | 说明 |
|---|---|
| `RigidBody2D` | Static / Kinematic / Dynamic、质量、阻尼、重力系数、固定旋转、子弹模式（CCD）、碰撞层 |
| `BoxCollider2D`、`CircleCollider2D`、`CapsuleCollider2D`、`PolygonCollider2D`（≤ 8 顶点，超过时自动凸分解） | 基础形状 |
| `EdgeCollider2D` / `ChainCollider2D` | 线段链，用于地形 |
| `TilemapCollider2D` | 从瓦片地图生成合并后的链形碰撞体 |
| `Joint2D` 系列 | Distance、Revolute、Prismatic、Wheel、Weld、Mouse、Motor |
| `CharacterController2D` | 基于形状投射的运动学角色控制器（Box2D v3 提供 mover 相关工具函数） |

### 13.4 同步与线程

- 物理在固定步长中运行（默认 60Hz）。渲染帧率高于物理时，渲染用的变换在两个物理状态之间插值（`RigidBody` 的 `Interpolate` 选项，默认开启）。
- Jolt 使用引擎任务系统（实现 `JPH::JobSystem`），Box2D v3 通过 `b2WorldDef` 中的 `enqueueTask` / `finishTask` 回调接入引擎任务系统，两者都能多线程运行。
- 同步顺序：
  1. 新增/删除的刚体和碰撞体：在组件钩子中排队，物理步前统一创建/移除（Jolt 用 `BodyInterface::AddBodiesPrepare/Finalize` 批量添加）。
  2. Kinematic 物体：从 `WorldTransform` 读取目标位置，调用 `MoveKinematic`。
  3. 被 gameplay 代码直接修改了 `Transform` 的 Dynamic 物体：传送到新位置（并给出开发期警告，提示应该用力或速度控制）。
  4. 步进。
  5. Dynamic 物体：把位置写回 `Transform`（只写活跃的刚体，Jolt 提供活跃物体列表）。
- 碰撞事件：Jolt 的 `ContactListener` 回调在工作线程上触发，回调中只把事件写入线程安全的缓冲区；物理步结束后在主线程上去重、排序，再调用 GameObject 的 `OnCollisionEnter` 等虚函数。Box2D v3 本身就是在步进后提供事件数组（`b2World_GetContactEvents`），直接在主线程读取。

### 13.5 碰撞层

- 项目设置中定义最多 32 个碰撞层（名称可编辑），以及层与层之间是否碰撞的矩阵。3D 和 2D 共用一套层定义，各自独立生效。
- Jolt 映射：对象层（ObjectLayer）= 碰撞层；广相层（BroadPhaseLayer）固定为 Static、Dynamic、Trigger、Debris 几类，用于加速广相。
- Box2D 映射：`b2Filter` 的 `categoryBits` / `maskBits`。

### 13.6 查询接口

```cpp
namespace star::physics3d {
std::optional<RaycastHit> Raycast(const PhysicsScene&, Ray ray, float maxDist, const QueryFilter& = {});
void RaycastAll(const PhysicsScene&, Ray ray, float maxDist, std::vector<RaycastHit>& out, const QueryFilter& = {});
std::optional<ShapeCastHit> SphereCast(...);
void OverlapBox(..., std::vector<BodyHandle>& out);
}
```

- `QueryFilter` 包含层掩码、忽略的实体、是否包含触发器。
- 物理服务返回 `BodyHandle`；Scene 查询包装将其映射为 `ObjectHandle` 或 `ObjectRef<GameObject>`。游戏侧碰撞事件和查询结果不暴露底层 ECS 标识。
- 查询可以在任意线程调用（只读），但不能与物理步并发；物理步期间的查询在开发期会断言。

### 13.7 确定性

Jolt 在开启 `JPH_CROSS_PLATFORM_DETERMINISTIC` 编译选项后可以跨平台确定（代价是少量性能），Box2D v3 也以跨平台确定性为设计目标。引擎提供 `Physics.Deterministic` 项目设置，默认关闭。虽然引擎不内置网络，但开启后可用于回放系统，游戏自行实现锁步联机时也能用上。

---

## 14. 音频、输入、动画

### 14.1 音频 **[决定：miniaudio]**

结构：

```
AudioEngine（封装 ma_engine）
 ├── 输出设备管理（热插拔、切换默认设备）
 ├── 混音总线（Bus）：Master → Music / SFX / Voice / UI（项目设置中可增加）
 │     每条总线：音量、静音、效果链（低通、高通、混响、压缩）
 ├── 声音实例池（ma_sound）
 └── 流式播放（长音频从 VFS 流式读取，自定义 ma_data_source）
```

组件：

| 组件 | 说明 |
|---|---|
| `AudioSource` | 音频片段或随机容器、总线、音量、音高、循环、空间化（2D/3D）、衰减曲线（线性/对数/自定义）、最小/最大距离、多普勒、自动播放 |
| `AudioListener` | 通常挂在相机上；每个 World 同一时刻只有一个生效 |
| `AudioReverbZone`（第二阶段） | 区域混响 |

实现要点：

- miniaudio 的解码器直接支持 WAV、FLAC、MP3；Ogg Vorbis 需要接入 stb_vorbis（miniaudio 的官方扩展方式）。烘焙时短音效转为 PCM/ADPCM，长音乐保留 Vorbis 流式。
- 音频文件读取通过自定义 `ma_vfs` 接入引擎 VFS，从而能读 pak。
- 3D 空间化使用 miniaudio 内置的空间化器（声像 + 衰减 + 多普勒）。如果以后需要 HRTF，可以接入 Steam Audio（Apache 2.0）。
- 线程：音频逻辑（播放、停止、参数修改）在主线程调用，miniaudio 内部的混音在音频线程上完成，参数修改是线程安全的。
- 声音实例数上限和优先级：超出上限时停止优先级最低、最远的实例（虚拟声音：只计时不混音）。

### 14.2 输入

两层结构：

1. **原始输入层**（Platform）：SDL3 事件 → 键盘、鼠标、手柄（SDL3 gamepad API，统一 Xbox/PlayStation/Switch 布局）、触摸（预留）、文字输入（IME）。
2. **动作映射层**（Input 模块）：
   - `InputActions` 资产定义动作（`Jump`、`Move`、`Look`）和绑定（键盘 Space、手柄 A；`Move` 绑定到 WASD 组合和左摇杆）。
   - 动作类型：Button（按下/抬起/持续）、Axis1D、Axis2D。
   - 修饰器：死区、反转、缩放、平滑、按住时长（长按）、多击（双击）。
   - 上下文（Action Map）：`Gameplay`、`Menu`、`Vehicle`，可以叠加启用，按优先级消费输入（UI 获得焦点时屏蔽 gameplay 输入）。
   - 运行时重新绑定按键，并保存到用户配置。
   - 本地多人：每个 `PlayerInput` 实例关联一组设备。

Gameplay 代码中的用法：

```cpp
void PlayerCharacter::OnBeginPlay() {
    auto& input = GetComponent<PlayerInput>();
    input.Bind("Jump", InputEvent::Pressed, this, &PlayerCharacter::Jump);
}
void PlayerCharacter::OnTick(float dt) {
    Vec2 move = GetComponent<PlayerInput>().ReadAxis2D("Move");
    // ...
}
```

### 14.3 动画

#### 骨骼动画 **[建议：ozz-animation]**

- 运行时采样、混合、局部到模型空间转换使用 ozz-animation（MIT），它的运行时格式紧凑，采样经过 SIMD 优化。
- 导入时把 glTF/FBX 中的骨骼和动画转换为 ozz 格式（ozz 提供离线转换库，引擎导入器调用它，输入是 ufbx/fastgltf 解析后的数据）。
- 组件：
  - `SkeletalMeshRenderer`：蒙皮网格 + 骨骼。
  - `Animator`：引用一个 `AnimationGraph` 资产，保存运行时状态和参数（float/int/bool/trigger）。
- 动画图（`AnimationGraph`）：
  - 状态机（状态、转换、转换条件、混合时长、可中断设置）。
  - 混合空间（1D/2D）。
  - 分层（上下半身分离，骨骼遮罩）、叠加动画。
  - 动画事件（脚步声、攻击判定帧），采样后在主线程分发到 GameObject。
  - 根运动（Root Motion）：提取到角色控制器。
  - IK：双骨 IK（脚部贴地、手部抓取）和 look-at，第一阶段做双骨 IK。
- 并行：每个 `Animator` 的采样是独立任务，在 Animation 阶段 `ParallelFor`。
- 编辑器：动画图编辑器（节点图）、动画预览窗口（第二阶段完成完整编辑器，第一阶段先提供 YAML 编辑 + 预览）。

#### 精灵动画

- `SpriteAnimation` 资产：帧列表（精灵引用 + 时长）、循环模式、帧事件。
- `SpriteAnimator` 组件，可以复用同一套 `AnimationGraph` 状态机（状态节点改为播放精灵动画）。

#### 属性动画（第二阶段）

- 基于反射的时间轴：对任意反射属性做关键帧动画（UI 动画、过场、灯光闪烁），曲线编辑器在编辑器中实现。

---

## 15. 游戏 UI 系统 **[决定：自研 retained-mode UI]**

Dear ImGui 只用于编辑器和运行时调试面板。发布给玩家的 UI 使用自研的保留模式 UI 系统。

### 15.1 结构

```
UI 模块
 ├── UIDocument        一棵元素树（从 .ui 资产实例化，或由代码构建）
 ├── 元素（Element）    Panel、Image、Text、Button、Toggle、Slider、ScrollView、TextInput、ListView、Dropdown、ProgressBar
 ├── 布局               Yoga（MIT，Flexbox）+ 绝对定位 + 锚点
 ├── 样式               .uss 样式表：选择器（类型、类名、ID、伪类 :hover :pressed :focus :disabled）、继承、变量
 ├── 文字               FreeType 光栅化 + HarfBuzz 排版 + MSDF 图集；支持 BiDi（第二阶段）、富文本标签、字体回退链（CJK）
 ├── 事件               指针（鼠标/触摸）、导航（手柄/键盘方向键焦点切换）、文本输入（IME）、冒泡与捕获
 ├── 动画               样式属性过渡（transition）、关键帧动画
 ├── 数据绑定           元素属性绑定到反射属性（单向/双向），减少胶水代码
 ├── 本地化             字符串表资产（按语言），文本元素引用键名
 └── 渲染               生成批处理后的顶点数据，交给 Renderer 的 UI pass
```

### 15.2 文件格式

`.ui`（YAML）描述元素树，`.uss` 描述样式（类 CSS 语法，自研解析器，只支持需要的子集）：

```yaml
# Content/UI/MainMenu.ui
StarFile: UIDocument
Styles: [ { Asset: <guid of MainMenu.uss> } ]
Root:
  Type: Panel
  Class: [root]
  Children:
    - Type: Text
      Id: title
      Text: "@menu.title"           # An @ prefix marks a localization key.
    - Type: Button
      Id: start
      Class: [menu-button]
      OnClick: StartGame            # Binds a reflected controller function.
      Children:
        - { Type: Text, Text: "@menu.start" }
```

```css
/* MainMenu.uss */
.root { flex-direction: column; align-items: center; justify-content: center; width: 100%; height: 100%; }
.menu-button { width: 320px; height: 64px; margin: 8px; background-color: var(--panel-color); transition: scale 0.1s; }
.menu-button:hover { scale: 1.05; }
```

### 15.3 使用方式

- `UIDocumentComponent`：挂在 GameObject 上，指定 `.ui` 资产、渲染目标（屏幕空间 / 世界空间 / 渲染到纹理）、排序顺序、控制器对象。
- 控制器：一个 GameObject 子类或普通反射类，`.ui` 中的事件绑定到它的 `STAR_FUNCTION`，数据绑定到它的 `STAR_PROPERTY`。
- 代码中可以按 ID 查询元素：`doc.Query<Button>("start")->onClick.Add(...)`。
- 分辨率适配：参考分辨率 + 缩放模式（按宽、按高、按较小边、固定像素），支持安全区域。

### 15.4 编辑器支持

- UI 编辑器面板：所见即所得的元素树编辑、样式检视器、布局预览（多种分辨率/宽高比切换）。
- 第一阶段可以先只做预览 + 热重载（直接编辑 `.ui`/`.uss` 文件，保存后立即刷新），第二阶段再做拖拽式可视化编辑。

---

## 16. 游戏模块与热重载

### 16.1 游戏模块接口

游戏模块是一个动态库（`MyGame.dll` / `libMyGame.so` / `libMyGame.dylib`），导出唯一的 C 入口：

```cpp
extern "C" STAR_EXPORT const star::ModuleDescriptor* StarGetModuleDescriptor();

struct ModuleDescriptor {
    uint32_t    abiVersion;              // Reject incompatible engine ABI versions.
    const char* name;
    std::expected<void, Error> (*registerTypes)(reflect::TypeRegistry&);
    std::expected<void, Error> (*startup)(Engine&);
    void (*shutdown)(Engine&);
};
```

- 一个项目可以有多个游戏模块（例如 `MyGame`、`MyGameEditor`，后者只在编辑器中加载，用来放自定义检视器和编辑器工具）。
- 发布版中游戏模块静态链接，`StarGetModuleDescriptor` 通过静态注册表找到，不需要动态加载。

### 16.2 ABI 约束

游戏模块和引擎动态库之间的边界是 C++ ABI，因此：

- 游戏模块必须用与引擎完全相同的编译器、标准库、编译选项（`_ITERATOR_DEBUG_LEVEL`、运行时库 `/MD`、`-fno-...` 等）构建。项目模板通过 `find_package(StarEngine)` 继承引擎的编译设置，用户不应修改这些选项。
- 引擎二进制发布时附带它的 `StarEngineConfig.cmake`，记录编译器版本，CMake 配置时检查并给出明确错误。
- 公共 ABI 发生不兼容变化时递增 `abiVersion`；当前通过本地模块兼容性测试验证。`StarGetModuleDescriptor` 不注册类型、不启动任务；可恢复的注册或启动错误通过 `expected` 返回，不用断言表示。
- 单一分配器（见 6.1），保证跨 DLL 分配和释放安全。

### 16.3 热重载流程

**[决定]** 第一版只在编辑器安全点热重载；若正在 PIE，预检成功后先停止 PIE 再切换，不迁移运行中的模拟状态，也不自动重新开启。成功或回滚都会保留未保存的编辑内容；热重载不自动写回源场景。

1. 构建成功后为新模块创建唯一加载路径，保留当前版本的文件。在不调用新模块注册和启动回调的情况下检查入口、ABI 及依赖版本；构建失败或预检失败不替换当前模块。
2. 到达阶段边界并提交已接收的场景修改；停止进入旧模块的新任务，等待已有的任务和回调结束。在旧模块仍有效时，将受影响的编辑世界和文档捕获为引擎拥有的快照。另存脏标记、GUID 选择集及撤销/重做记录，快照包含 §8.3 的未知数据。快照失败则取消重载，不卸载旧模块。
3. 释放旧模块的实例、组件池和待提交命令，取消其 System、委托、导入器、CVar 等注册；销毁依赖模块的反射缓存、元数据和函数指针后再卸载旧模块。多模块按依赖的逆序卸载、正序加载；有依赖方时一起重载。
4. 在编辑器安全点注册新模块、启动并从快照恢复候选状态。按 GUID、类型与字段名匹配；新增字段取默认值，无法匹配的类型或字段按 §8.3 作为占位或扩展数据保留。候选状态恢复并验证完成后，才提交为可见状态、恢复选择及撤销/重做，再刷新编辑器面板。
5. 新模块加载、注册、启动或恢复发生可恢复的失败时，先撤销候选模块已经产生的注册与实例，卸载候选模块，再从保留的旧模块和同一快照恢复原状态。若旧模块也无法恢复，保留引擎拥有的快照和诊断到 `Saved/Recovery/`，进入恢复模式，不覆盖源文件或继续使用半恢复状态。

撤销/重做记录以 GUID、属性路径、序列化值与引擎自己的命令表示保存，不持有游戏类型的实例或回调。新版本暂时无法解析的记录保留，执行时明确报错而不破坏记录；回滚成功后旧版本可以继续使用。热重载本地集成测试必须覆盖成功迁移、新模块失败回滚、保留脏文档与未知数据，以及旧模块卸载后没有残留回调。

### 16.4 平台细节

| 问题 | Windows | Linux | macOS |
|---|---|---|---|
| DLL 被锁定无法覆盖 | 每次构建输出唯一文件名（`_hrNNNN`），或加载前复制到临时目录 | 不锁定，但仍使用唯一文件名，避免 `dlopen` 返回缓存句柄 | 同 Linux |
| 调试符号 | PDB 也必须改名，否则 VS 调试器锁定 PDB 导致链接失败：链接选项 `/PDB:MyGame_hr0003.pdb` | 符号在 .so 中或 split DWARF | dSYM 或嵌入式调试信息 |
| 静态初始化/析构 | 游戏模块中的全局对象会在卸载时析构，禁止在游戏模块中保存指向引擎对象的全局指针（除非在 shutdown 中清空） | 同 | 同 |
| `dlclose` 不真正卸载 | — | 使用了 `STB_GNU_UNIQUE` 符号（例如某些模板静态变量、`inline` 变量）时 `dlclose` 不会卸载，需要用 `-fno-gnu-unique` 编译游戏模块 | 带 Objective-C 类或 Swift 代码的 dylib 不能卸载，游戏模块禁止包含这类代码 |
| 线程局部变量 | 卸载前确保没有工作线程正在执行模块中的代码：热重载前任务系统排空 | 同 | 同 |

### 16.5 增量编译速度

热重载的体验取决于编译速度：

- 游戏模块使用 PCH（引擎公共头文件的常用子集）。
- 推荐游戏代码按功能拆分成较小的 `.cpp`；引擎头文件尽量减少传递包含（前置声明、Pimpl）。
- 推荐链接器：Windows 用 `lld-link` 或 MSVC 增量链接；Linux 用 mold；macOS 用 ld-prime（Xcode 15+ 默认）。
- 第二阶段可以评估 Live++（商业）作为补充，它能在不卸载模块的情况下打补丁，适合不改类布局的函数体修改。

---

## 17. 编辑器

### 17.1 进程与启动

- `StarEditor` 是独立可执行文件。启动参数：`StarEditor <path>/MyGame.starproject`；不带参数时显示项目浏览器（新建项目、打开最近项目、从模板创建）。
- 启动顺序：加载引擎模块 → 读取项目文件 → 检查游戏模块是否需要编译（源文件比二进制新时提示编译）→ 加载游戏模块 → 扫描 `Content/` 并更新资产数据库 → 打开上次的场景和布局。
- 编辑器持有一个"编辑器世界"（Editor World），PIE 时创建一个"游戏世界"（PIE World）。

### 17.2 UI 框架

- Dear ImGui docking 分支 + 多视口（viewports），窗口可以拖出主窗口成为独立的系统窗口。多视口的每个系统窗口由 SDL3 创建，交换链由引擎 RHI 创建。
- ImGui 渲染后端：引擎自己实现一个基于 RHI 的 ImGui 渲染器（顶点缓冲区 + 纹理），所以四个图形后端都能运行编辑器，不需要维护四份 ImGui 官方后端。
- ImGui 纹理 ID 映射到 RHI 的 `TextureView` 句柄，视口面板直接显示渲染图的输出纹理。
- 字体：思源黑体 / Noto Sans CJK（支持中文）+ 图标字体（Font Awesome 或 Material Symbols）。ImGui 1.92+ 支持动态字形加载，不需要预先烘焙全部 CJK 字形。
- 主题：深色主题为默认，提供浅色主题；DPI 缩放跟随系统，可手动调整。
- 布局保存到 `Saved/Editor/Layout.ini`，提供"恢复默认布局"。
- 附加库：ImGuizmo（变换操纵器）、imnodes 或 imgui-node-editor（节点图）、ImPlot（曲线与分析图）。

### 17.3 面板清单

| 面板 | 功能 | 阶段 |
|---|---|---|
| 场景视口（可多个） | 透视/正交/2D 视图、相机飞行控制（WASD + 右键）、聚焦（F）、变换操纵器（W/E/R）、本地/世界坐标切换、网格吸附与角度吸附、多选框选、拖放资产到场景、显示模式（光照、无光照、线框、法线、overdraw、LOD 着色）、渲染统计 | 1 |
| 层级（Hierarchy） | 按场景分组的对象树、拖放改变父子关系、搜索与过滤、显示/隐藏、锁定、右键菜单（创建、复制、删除、创建预制体）、预制体实例标记 | 1 |
| 检视器（Inspector） | 反射驱动的属性编辑、组件添加/移除/排序、多选时编辑共同属性、预制体覆盖显示与还原、自定义属性绘制器 | 1 |
| 内容浏览器（Content Browser） | 目录树 + 网格/列表视图、缩略图（后台生成并缓存）、搜索（名称、类型、标签）、导入（拖放外部文件）、重命名/移动（同步 `.meta`）、引用查看器（谁引用了这个资产）、导入设置编辑 | 1 |
| 控制台 / 日志 | 日志分类过滤、搜索、点击跳转到源码位置、命令输入（CVar、控制台命令，带自动补全） | 1 |
| 项目设置 | 反射驱动：渲染、物理（碰撞层矩阵）、输入、音频总线、启动场景、打包设置 | 1 |
| 编辑器偏好 | 快捷键、主题、外部 IDE 路径、自动保存间隔 | 1 |
| 材质编辑器 | 第一阶段：参数检视器 + 预览球；第二阶段：节点图 | 1 / 2 |
| 网格预览 | 网格、子网格、LOD、骨骼显示、碰撞体显示 | 1 |
| 纹理预览 | mip、通道、压缩格式、内存占用 | 1 |
| 性能分析 | CPU/GPU 帧时间图、Tracy 连接按钮、内存统计（按标签）、渲染图可视化 | 1 |
| 构建与打包 | 平台、配置、输出目录、一键打包、日志 | 1 |
| 瓦片地图编辑器 | 瓦片集面板、画笔/填充/矩形/选择工具、多图层、自动瓦片规则（第二阶段） | 1 |
| 精灵编辑器 | 切片（网格/自动）、轴心点、九宫格边距、物理形状 | 1 |
| 动画图编辑器 | 状态机节点图、混合空间编辑、参数面板、实时预览 | 2 |
| 动画预览 | 时间轴、播放控制、骨骼显示、根运动显示 | 1 |
| 曲线/时间轴编辑器 | 属性动画、过场编辑 | 2 |
| UI 编辑器 | 见 15.4 | 1（预览）/ 2（可视化） |

### 17.4 选择、命令与撤销

- **选择集**：保存对象 GUID（而不是指针），热重载、PIE 切换后可以恢复。
- **命令模式**：所有修改场景或资产的操作都是 `EditorCommand`，实现 `Execute()` 和 `Undo()`。
  - 属性修改命令：记录对象 GUID、属性路径、旧值和新值（反射序列化成小块二进制）。检视器中连续拖动滑块合并为一个命令（拖动开始时开始事务，结束时提交）。
  - 结构修改命令（创建、删除、改父子关系、添加/移除组件）：删除时保存被删对象的完整序列化数据，撤销时原样恢复（GUID 不变）。
  - 复合命令：多个命令组成一个事务，一次撤销。
- 撤销栈按"文档"（场景、材质、UI 文档等）分开，切换文档时撤销栈跟着切换。需要跨重载保留的记录不持有模块实例或回调（见 §16.3）。
- 脏标记：命令执行后标记文档为已修改，标题栏和标签页显示 `*`。
- 自动保存：定期把未保存的文档写到 `Saved/Autosave/`，崩溃后重新打开编辑器时提示恢复。

### 17.5 Play In Editor（PIE）

1. 点击"播放"：把编辑器世界中所有已加载场景序列化成内存快照（二进制），在新的 PIE World 中反序列化，保留占位和扩展数据，但不运行缺失类型的逻辑，再为可解析的游戏对象调用 `OnBeginPlay`。
2. 运行期间：视口切换到游戏相机；可以暂停、单步（逐帧）；可以弹出"模拟模式"（运行物理和逻辑但用编辑器相机）。
3. 运行期间检视器显示 PIE World 中的对象，修改不影响编辑器世界（检视器顶部提示"运行中的修改不会保存"）。提供"保留修改"右键菜单，把某个对象或组件的当前值复制回编辑器世界。
4. 点击"停止"：销毁 PIE World，回到编辑器世界。编辑器世界在 PIE 期间保持不动，所以不需要恢复。
5. 第二阶段：在独立进程中运行游戏（`GameLauncher` 读取编辑器的松散资产），用于测试多窗口、全屏、性能和崩溃隔离。

### 17.6 编辑器扩展

游戏项目可以在 `MyGameEditor` 模块中扩展编辑器：

- 自定义组件/类型的检视器：`STAR_CLASS(CustomInspector = "MyHealthInspector")` 或注册 `InspectorDrawer<T>`。
- 自定义属性绘制器：按类型或按元数据（`STAR_PROPERTY(Edit, Drawer = "ColorGradient")`）。
- 自定义面板：继承 `EditorPanel`，注册到"窗口"菜单。
- 自定义菜单项和工具栏按钮：`EditorMenu::Register("Tools/Rebuild NavMesh", fn)`。
- Gizmo 绘制：组件可以提供 `OnDrawGizmos(DebugDraw&)`（选中时或总是绘制）。
- 自定义资产类型和导入器。

### 17.7 与 IDE 集成

- 编辑器提供 Ninja 构建生成的 `compile_commands.json`，并可按偏好在 IDE 中打开源码。IDE 只用于编辑和调试；构建与热重载调用 CMake + Ninja，不生成 MSBuild 或 Xcode 工程。
- 日志和检视器中的源码位置可点击跳转（调用 IDE 的命令行打开文件到指定行）。
- 新建 GameObject 子类或组件：编辑器提供"新建 C++ 类"向导，按模板生成 `.hpp`/`.cpp` 并加入 CMake（游戏模块的 CMake 使用 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 收集源文件，所以不需要编辑 CMakeLists）。

---

## 18. 打包与发布

### 18.1 打包流程

命令：`star package --project MyGame.starproject --platform <windows|linux|macos> --config shipping --output Build/`。编辑器的"构建与打包"面板调用同一命令。

```
1. 编译：以 Shipping 预设构建 GameLauncher + 引擎静态库 + 游戏模块（静态链接成单一可执行文件）
2. 烘焙：star cook（见 10.8），只烘焙可达资产
3. 打包资产：生成 Game.pak（可按项目设置拆分为多个 pak，如按关卡分块）
4. 收集运行时文件：D3D12 Agility SDK、平台必需的动态库（见下表）、许可证文件
5. 平台打包：生成目录结构 / .app bundle / 压缩包
6. 签名（可选，需要配置证书）
7. 输出构建报告：包体大小按资产类型分类、烘焙警告、未引用资产列表
```

### 18.2 平台产物

| 平台 | 产物结构 | 说明 |
|---|---|---|
| Windows | `MyGame/MyGame.exe`、`D3D12/D3D12Core.dll`、`D3D12/d3d12SDKLayers.dll`（仅开发版）、`Content/Game.pak` | 静态链接 MSVC 运行时（`/MT`）还是附带 VC++ 运行库安装包，由项目设置选择，默认 `/MD` 并附带 `vc_redist`；可选生成 MSIX 或 Inno Setup 安装包 |
| Linux | `MyGame/MyGame`（可执行文件）、`MyGame/lib/`（必要的 .so）、`Content/Game.pak` | RPATH 设为 `$ORIGIN/lib`；在 Steam Runtime 3 (sniper) 容器中构建以保证 glibc 兼容；可选生成 AppImage 或 tar.gz；Steam 发布时使用 Steam Linux Runtime |
| macOS | `MyGame.app/Contents/MacOS/MyGame`、`Contents/Resources/Game.pak`、`Contents/Info.plist`、`Contents/Frameworks/`（如有） | 默认 arm64；尽力支持可选 x86_64/universal（arm64 + x86_64）；`codesign` + `notarytool` 公证（需要 Apple Developer 账号）；`.dmg` 或 `.zip` |

运行时动态依赖控制在最少：

- Windows：系统 DLL + D3D12 Agility SDK。Vulkan 后端依赖显卡驱动自带的 `vulkan-1.dll`（通过 volk 动态加载，没有时自动回退到 D3D12）。
- Linux：`libc`、`libm`、`libdl`、`libpthread`、`libvulkan.so.1`（驱动提供，volk 动态加载）、X11/Wayland 客户端库（SDL3 动态加载）、ALSA/PulseAudio/PipeWire（miniaudio 动态加载）。其他第三方库全部静态链接。
- macOS：系统框架（Metal、QuartzCore、AppKit、AudioToolbox、GameController）。

### 18.3 发布版运行时行为

- 资产只从 pak 读取（开发版允许松散文件）。
- 日志写到用户目录（Windows `%LOCALAPPDATA%/<Company>/<Game>/Logs`，Linux `$XDG_DATA_HOME/<game>/logs`，macOS `~/Library/Logs/<Game>`）。
- 崩溃处理：写出 minidump 或崩溃报告（调用栈 + 日志尾部 + 系统信息），保存在本地；上传到崩溃收集服务（例如自建 Sentry 或 Backtrace）是可选的项目设置，默认关闭，开启前需要在游戏中取得玩家同意。
- 发布版保留一个最小的开发者控制台（默认关闭，可以在项目设置中完全移除）。
- 用户设置（画质、按键绑定、音量）保存在用户目录的 YAML 文件中。

### 18.4 版本与补丁

- 项目设置中的版本号写入可执行文件（Windows 资源、macOS `Info.plist`）和 pak 头部。
- 补丁：生成只包含变化资产的补丁 pak（按内容哈希比较两次烘焙结果），挂载优先级高于基础包。
- 引擎本身的版本号遵循语义化版本，游戏项目文件中记录使用的引擎版本，编辑器打开不匹配版本的项目时提示升级。

---

## 19. 本地测试与质量保障

### 19.1 本地验证范围

**[决定]** 项目当前由一人维护，允许大量使用 AI 辅助开发。当前仅维护能在本地构建、运行的测试代码；不搭建 CI、不要求定时任务或自托管 runner。AI 生成的改动与手写改动执行同样的本地验证。尚未验证的组合应明确记录，不因测试没跑而视为通过。

| 层次 | 本地验证内容 | 何时运行 |
|---|---|---|
| 单元测试 | doctest（建议）接入 CTest；Core、序列化、Jobs、句柄、组件与层级等 | 相关改动后运行；M0 建立入口 |
| 模块集成 | Null RHI；反射干净/增量生成、场景往返、外部游戏 SDK、快照热重载与失败回滚 | 对应模块可运行后，改动相关接口时运行 |
| 渲染与着色器 | 对应平台 GPU 上运行示例和验证层，检查实际 RHI；本地编译对应着色器目标 | 后端里程碑与发布前；图像基准比较可随渲染测试完善 |
| 编辑器与打包 | 本地打开项目、进入 PIE、打包并从输出目录启动 | 工具就绪后，发布前在正式平台执行 |
| 性能 | 必要时记录参考硬件、场景、画质与帧时；稳定后再定回退阈值 | 不设置定时任务，不作为 M0 门槛 |

### 19.2 本地平台矩阵

| 正式目标 | 工具链入口 | 发布前本地验证 |
|---|---|---|
| Windows x64 | MSVC + CMake + Ninja；clang-cl 为可选补充 | 本地测试、编辑器和 Shipping 示例；分别验证 D3D12、Vulkan、D3D11，并验证回退 |
| Linux x64 | GCC 或 Clang + CMake + Ninja；Shipping 使用固定的本地容器 | 本地测试、编辑器和 Shipping 示例；验证 Vulkan 及 X11 / Wayland |
| macOS arm64 | AppleClang + CMake + Ninja | 在 Apple Silicon 实机运行本地测试、编辑器和 Shipping 示例；验证 Metal |
| macOS x86_64 | 可选，尽力支持 | 有条件时构建和运行，不阻塞发布 |

M0 提供同名的配置、构建与测试预设。例如在 Linux 主机依次运行 `cmake --preset linux-dev`、`cmake --build --preset linux-dev`、`ctest --preset linux-dev --output-on-failure`；另外两平台换成 `win-dev`、`mac-dev`。这些是脚手架的交付接口，不要求每次修改都切换三台机器；各正式平台在对应里程碑和发布前验收。发布候选应记录 OS、架构、编译器、GPU/驱动、启用的 RHI 及测试结果；最低支持环境未测时显式记录，不用新版环境测试结果代替。

### 19.3 工程检查与 Git 规则

- 在本地运行格式检查和相关测试；公共头文件变化时检查独立编译与 SDK 依赖边界，ABI 或序列化变化时补版本检查、迁移或往返测试。
- 维护者对 AI 辅助改动同样复核接口、线程与资源生命周期、跨平台代码、失败路径及新增第三方依赖。依赖的用途、许可证与选型仍记录在 `docs/adr/`。
- 分支、Conventional Commits、代理授权和推送前检查以 [Git 工作规则](../../agent-rules/git.md) 为准。PR、批准与签名规则后续再定；当前不把 CI 设为合并门槛。

### 19.4 决策变更流程

修改本文中标注 **[决定]** 的条目，由维护者在 ADR（`docs/adr/NNNN-kebab-case.md`）记录背景、备选方案、决定和后果，并同步更新本文档；不因此引入 Git 审批流程。

---

## 20. 里程碑与路线图

阶段划分以每个里程碑都有可在本地运行的交付物为原则。按单人维护、AI 辅助开发推进；不沿用原多人排期，当前不承诺各阶段工期。

### M0 基础设施

- 仓库结构、CMake + Ninja 模块函数与预设、依赖策略（系统包 + vcpkg）、本地 CTest 入口和代码风格配置。
- Core：内存、容器、日志、断言、数学、GUID、Name、委托、配置、CVar。
- Platform：SDL3 窗口、事件、文件系统、动态库加载、时钟。
- Jobs：任务系统 + 单元测试。
- **验收**：Windows x64、Linux x64 和 macOS arm64 的本地预设能构建并跑通测试；示例程序打开窗口并打印帧时间。

### M1 反射与序列化（可与 M2 并行）

- StarHeaderTool（libclang 解析、生成、缓存、错误报告）。
- 运行时类型注册表、容器操作、元数据。
- YAML 与二进制 Archive，反射驱动序列化，版本升级机制。
- **验收**：从空构建目录完成生成；验证各类属性和未知数据的 YAML/二进制往返，直接与传递输入变化能触发重生成，只改不相关实现文件不触发重生成。

### M2 RHI 与 Vulkan 后端

- RHI 接口、Null 后端、Vulkan 后端（含 VMA、验证层、调试名、时间戳）。
- Slang 着色器编译集成（编辑器/工具侧），SPIR-V 输出。
- 渲染图第一版（屏障计算、瞬态资源）。
- **验收**：在 Windows 和 Linux 上用 Vulkan 渲染一个带纹理的旋转网格，RenderDoc 中 pass 名和资源名正确。

### M3 Gameplay 框架

- Scene、World、GameObject、Scene 私有 EnTT 集成、只依赖 Star SDK 的查询与命令接口、游戏模块可定义并注册组件、内置组件（Transform、Hierarchy、Camera、MeshRenderer、Light）。
- 变换传播、生命周期、延迟命令、Tick 阶段、System 调度。
- 场景 YAML 保存/加载、预制体（不含嵌套）。
- 游戏模块加载（还没有热重载）。
- **验收**：独立游戏模块不引用 EnTT 即可注册 GameObject 与组件；引擎和游戏读到同一组件值，结构命令在阶段边界可见，场景可往返，卸载测试模块后能重新注册（完整编辑器热重载在 M5）。

### M4 资产管线与 3D 渲染基础

- 资产数据库、`.meta`、导入器（纹理、glTF、FBX）、DDC、异步加载、引用计数。
- Forward+ 管线：深度预通道、分簇光照、CSM 阴影、PBR、IBL、色调映射。
- 材质系统第一版（参数 + 变体）。
- **验收**：导入并渲染固定标准场景（如 Sponza），核对关键渲染效果并记录参考机器、画质与帧时；是否设性能阈值在建立本地基准后决定。

### M5 编辑器第一版

- ImGui 集成（RHI 渲染后端、多视口）、主框架、停靠布局。
- 视口、层级、检视器、内容浏览器、控制台、项目设置。
- 选择、变换操纵器、撤销/重做、PIE。
- 热重载（第 16 章完整流程）。
- **验收**：本地编辑场景且不保存，热重载成功后编辑状态与 PIE 正常；用失败模块验证能回滚并保留未保存内容、选择及撤销/重做记录。

### M6 物理、音频、输入、2D

- Jolt 集成（刚体、碰撞体、角色控制器、关节、查询、事件）、Box2D v3 集成。
- miniaudio 集成（组件、总线、流式播放）。
- 输入动作映射。
- 2D 管线：精灵批处理、图集、瓦片地图（含编辑器）、2D 物理组件。
- **验收**：一个 3D 示例（角色在关卡中行走、推箱子、有音效）和一个 2D 示例（平台跳跃，瓦片地图关卡）。

### M7 D3D12 与 Metal 后端（可从 M4 后期开始）

- D3D12 后端（Agility SDK、D3D12MA、描述符堆、Enhanced Barriers、DRED）。
- Metal 后端（metal-cpp、argument buffers、MTLHeap、二进制档案）。
- bindless 路径与 GPU 驱动剔除（Modern 层级）。
- 在本机测试 Windows D3D12、Linux Vulkan、macOS arm64 Metal 的对应着色器和渲染路径。
- **验收**：示例在这三个后端均能本地运行并通过同一功能检查；后端基准图如已建立，则分别与各自基准比较，不要求跨后端逐像素相同。

### M8 动画与游戏 UI

- ozz-animation 集成、动画图（状态机、混合空间、分层、事件、根运动）、双骨 IK、精灵动画。
- 游戏 UI：元素、Yoga 布局、样式表、文字（FreeType/HarfBuzz/MSDF，CJK）、事件与手柄导航、本地化。
- **验收**：示例游戏有动画角色和完整的主菜单、设置菜单、HUD。

### M9 打包与发布

- 烘焙、pak、三平台打包、签名与公证流程、崩溃处理、发布版运行时行为。
- 着色器变体清单与 PSO 预热。
- **验收**：两个示例游戏在三平台生成可分发包，在没有安装开发环境的干净机器上运行。

### M10 D3D11 后端与稳定化

- D3D11 后端（Basic 层级的全部降级路径）。
- 性能优化、文档、示例完善、1.0 发布准备。
- **验收**：Windows 上编辑器与 Shipping 示例在显式选择 D3D11 时使用 Basic 路径；D3D12、Vulkan 不可用时能正确回退到 D3D11。以日志和运行结果核对实际后端；此项阻塞 1.0。

### 第二阶段（1.0 之后）

节点式材质编辑器、动画图可视化编辑、UI 可视化编辑器、光照贴图烘焙、地形与植被、贴花、体积雾、SSR、车辆、布娃娃、属性动画/时间轴、独立进程 PIE、共享 DDC、导航网格（Recast/Detour）。

### 依赖关系

```
M0 ─┬─► M1 ─┬─► M3 ─► M4 ─► M5 ─► M6 ─► M8 ─┬─► M9 ─► M10
    └─► M2 ─┘          └────► M7 ─────────────┘
```

M7 可从 M4 后期开始推进，并且必须在 M9 三平台发包验收前完成；依赖图表示技术先后，不假设单人项目有并行人力。

---

## 21. 风险清单

| 风险 | 影响 | 可能性 | 应对 |
|---|---|---|---|
| 四个图形后端维护成本过高 | 进度拖延，各后端质量参差 | 高 | Vulkan 先行；渲染比对测试覆盖所有后端；D3D11 最后实现并作为 1.0 兼容性验收项 |
| libclang 解析速度慢、与编译器行为不一致 | 构建变慢，生成器误报 | 中 | 只解析带标记的头文件；PCH；结果缓存；生成器有独立测试集；预留 C++26 反射迁移 |
| 私有 ECS 与游戏模块组件的边界 | 组件访问错乱，或卸载后仍持有模块函数指针 | 高 | 仅在引擎侧建池；M3 在本地测试外部游戏模块的注册、访问、清理及重注册 |
| 热重载在某平台上不稳定 | 开发体验差 | 中 | 第一版不支持 PIE 期间热重载；失败回滚；Linux 用 `-fno-gnu-unique`；集成测试覆盖 |
| C++23 标准库支持在三平台不一致 | 编译失败或行为差异 | 中 | 用 §5.3 的候选清单约束用法；在相关里程碑验证三个正式平台 |
| 系统包版本漂移 | 开发机之间行为不一致 | 中 | API 不稳定的库按依赖策略使用 vcpkg；检查最低版本；发布构建走 vcpkg 与固定源码依赖 |
| Slang 的 Metal/DXBC 输出不成熟 | 着色器在某后端上出错 | 中 | 着色器测试覆盖所有目标；必要时为单个着色器提供后端专用实现 |
| 跨平台烘焙（DXBC 只能在 Windows、metallib 只能在 macOS 上生成） | 不能在一台机器上为所有平台打包 | 确定 | 在对应本地主机上烘焙；开发期 Metal 允许运行时编译 MSL |
| 编辑器功能膨胀 | 核心功能没做完就在做边缘功能 | 高 | 按 17.3 的阶段表执行，第二阶段功能不进入 1.0 |
| Apple 平台的签名、公证和 SDK 更新 | 发布受阻 | 中 | M9 前准备开发者账号，在本机验证签名与公证流程 |
| GameObject 虚函数 Tick 在大量对象时成为瓶颈 | 帧率下降 | 中 | 默认不 Tick；性能测试监控；文档中的"何时写 System"规则 |

---

## 附录 A：第三方库清单

| 库 | 用途 | 许可证 | 依赖策略 | 备注 |
|---|---|---|---|---|
| EnTT | Scene 私有 ECS 实现 | MIT | VENDORED | 上游 Git 子模块固定为 3.14.0，不暴露给游戏 SDK |
| SDL3 | 窗口、输入、手柄 | zlib | SYSTEM_THEN_VCPKG | 需要 3.2+ |
| glm | 数学 | MIT | SYSTEM_THEN_VCPKG | |
| mimalloc | 内存分配 | MIT | SYSTEM_THEN_VCPKG | |
| ankerl::unordered_dense | 哈希表 | MIT | VCPKG_ONLY | 单头文件，也可 VENDORED |
| xxHash | 哈希 | BSD-2 | SYSTEM_THEN_VCPKG | |
| rapidyaml | YAML 解析 | MIT | VCPKG_ONLY | |
| zstd | 压缩 | BSD-3 | SYSTEM_THEN_VCPKG | |
| lz4 | 快速压缩 | BSD-2 | SYSTEM_THEN_VCPKG | |
| Tracy | 性能分析 | BSD-3 | VCPKG_ONLY | 客户端与查看器版本必须一致 |
| volk | Vulkan 加载器 | MIT | SYSTEM_THEN_VCPKG | |
| Vulkan-Headers | Vulkan 头文件 | Apache 2.0 / MIT | SYSTEM_THEN_VCPKG | |
| Vulkan Memory Allocator | Vulkan 内存 | MIT | SYSTEM_THEN_VCPKG | |
| D3D12 Agility SDK | D3D12 新特性 | Microsoft 许可证（可再分发） | VCPKG_ONLY（`directx-agility-sdk` 或 NuGet） | 仅 Windows |
| D3D12 Memory Allocator | D3D12 内存 | MIT | VCPKG_ONLY | 仅 Windows |
| metal-cpp | Metal C++ 封装 | Apache 2.0 | VENDORED | 仅 macOS，随 SDK 版本更新 |
| Slang | 着色器编译 | MIT（含 LLVM 例外的 Apache 2.0 组件） | VCPKG_ONLY 或官方二进制 | 仅编辑器/工具 |
| DXC | DXIL 编译（Slang 调用） | NCSA / MIT | 官方二进制 | 仅 Windows 编辑器 |
| libclang | StarHeaderTool | Apache 2.0 with LLVM exception | SYSTEM_THEN_VCPKG | 仅工具，≥ 18 |
| Dear ImGui（docking） | 编辑器 UI | MIT | VENDORED | 固定提交 |
| ImGuizmo | 变换操纵器 | MIT | VENDORED | |
| ImPlot | 图表 | MIT | VENDORED | |
| imnodes / imgui-node-editor | 节点图 | MIT | VENDORED | 二选一，M5 时评估 |
| Jolt Physics | 3D 物理 | MIT | VCPKG_ONLY | 编译选项（双精度、确定性）必须全项目一致 |
| Box2D v3 | 2D 物理 | MIT | VCPKG_ONLY | v3 与 v2 API 完全不同 |
| miniaudio | 音频 | MIT-0 / 公有领域 | VENDORED | 单头文件 |
| stb_vorbis | Ogg Vorbis 解码 | MIT / 公有领域 | VENDORED | |
| ozz-animation | 骨骼动画运行时 | MIT | VCPKG_ONLY | |
| fastgltf | glTF 导入 | MIT | VCPKG_ONLY | 仅编辑器 |
| ufbx | FBX/OBJ 导入 | MIT / 公有领域 | VENDORED | 单文件，仅编辑器 |
| meshoptimizer | 网格优化、LOD、meshlet | MIT | SYSTEM_THEN_VCPKG | 仅编辑器 |
| MikkTSpace | 切线生成 | zlib | VENDORED | 仅编辑器 |
| stb_image | 图片读取 | MIT / 公有领域 | VENDORED | 仅编辑器 |
| tinyexr | EXR 读取 | BSD-3 | VCPKG_ONLY | 仅编辑器 |
| bc7enc_rdo | BC 纹理压缩 | MIT / 公有领域 | VENDORED | 仅编辑器 |
| ISPC Texture Compressor 或 Compressonator | BC6H 压缩 | MIT | VCPKG_ONLY | 仅编辑器，M4 时二选一 |
| KTX-Software | KTX2 读写 | Apache 2.0 | SYSTEM_THEN_VCPKG | 仅编辑器 |
| FreeType | 字体光栅化 | FTL / GPLv2（选 FTL） | SYSTEM_THEN_VCPKG | FTL 要求在文档中致谢 |
| HarfBuzz | 文字排版 | MIT | SYSTEM_THEN_VCPKG | |
| msdfgen | MSDF 生成 | MIT | VCPKG_ONLY | |
| Yoga | Flexbox 布局 | MIT | VCPKG_ONLY | |
| V-HACD 或 CoACD | 凸分解 | BSD-3 / MIT | VCPKG_ONLY | 仅编辑器，M6 时二选一 |
| doctest | 单元测试 | MIT | SYSTEM_THEN_VCPKG | |

许可证要求：发布的游戏需要附带所有运行时库的许可证文本（打包流程自动收集到 `ThirdPartyLicenses.txt`）。本表中没有 GPL/LGPL 的运行时依赖；新增依赖时必须保持这一点，或者经 ADR 批准。

---

## 附录 B：术语表

| 术语 | 含义 |
|---|---|
| World | 一个独立运行的游戏世界实例，包含若干 Scene |
| Scene | 对象容器和存储单元，对应一个 `.scene` 文件，有自己的 EnTT registry |
| GameObject | 带 GUID、名称、生命周期和逻辑的对象，对应一个 entity，可被继承 |
| Component | 挂在 GameObject 上的纯数据结构体，通过 Star ECS 接口读写 |
| System | 对一类组件批量执行逻辑的类，由调度器按阶段运行 |
| Prefab | 可复用的 GameObject 树资产，实例可以覆盖属性 |
| RHI | Render Hardware Interface，图形 API 抽象层 |
| 渲染代理（Render Proxy） | 渲染器侧表示一个可渲染物体的数据，由组件同步而来 |
| 渲染图（Render Graph） | 每帧声明 pass 及其资源读写，自动计算屏障和资源复用的框架 |
| DDC | Derived Data Cache，派生数据缓存，存放导入后的资产数据 |
| 烘焙（Cook） | 把资产转换为目标平台的发布格式 |
| PIE | Play In Editor，在编辑器中运行游戏 |
| PSO | Pipeline State Object，管线状态对象 |
| Bindless | 着色器通过索引访问全局资源表，而不是逐个绑定资源 |
| ABI | 二进制接口，决定游戏模块与引擎动态库能否互相调用 |
| ADR | Architecture Decision Record，架构决策记录 |

---

## 附录 C：决策记录（ADR）索引

每条决策的正文位于 [`docs/adr/`](../adr/README.md)，按编号归档。本文档是这些决策的摘要；两者冲突时以 ADR 为准，并同步修正本文档。

| 编号 | 标题 | 决定 | 主要理由 |
|---|---|---|---|
| [ADR-0001](../adr/0001-gameplay-model.md) | Gameplay 模型 | GameObject（可继承）+ ECS；EnTT 只作私有实现 | 兼顾面向对象的易用性与 ECS 的批处理性能 |
| [ADR-0002](../adr/0002-scene-storage-and-hierarchy.md) | 场景存储与层级 | 场景内平级存储（SlotMap + GUID 哈希表），层级用 `Hierarchy` 组件 | 迭代快；变换传播可批量、可并行 |
| [ADR-0003](../adr/0003-graphics-backends.md) | 图形后端 | Vulkan、D3D12、D3D11、Metal（原生） | 三平台原生支持；D3D11 用于低端回退 |
| [ADR-0004](../adr/0004-shader-language.md) | 着色器语言 | Slang | 一份源码覆盖四个后端；支持模块与泛型 |
| [ADR-0005](../adr/0005-reflection.md) | 反射 | libclang 代码生成器，预留 C++26 反射迁移 | C++23 无静态反射；生成器功能最完整 |
| [ADR-0006](../adr/0006-build-and-dependencies.md) | 构建与依赖 | CMake + Ninja；Linux/macOS 系统包优先 + vcpkg 回退；Windows vcpkg；发布构建走 vcpkg 与固定源码依赖 | 开发便利与发布可移植性兼顾 |
| [ADR-0007](../adr/0007-physics.md) | 物理 | Jolt（3D）+ Box2D v3（2D） | 性能与 PhysX 同量级、多核扩展好、原生 macOS 支持、MIT 许可 |
| [ADR-0008](../adr/0008-editor-ui.md) | 编辑器 UI | Dear ImGui docking，RHI 自绘后端 | 开发速度快，四个后端统一 |
| [ADR-0009](../adr/0009-game-code-iteration.md) | 游戏代码迭代 | 游戏模块动态库 + 保留编辑数据、支持失败回滚的快照式热重载 | 不引入脚本语言的前提下提供快速迭代 |
| [ADR-0010](../adr/0010-cpp-modules.md) | C++ Modules | 第一版不用，保留迁移路径 | 三平台工具链支持不一致；生成器处理头文件更简单 |
| [ADR-0011](../adr/0011-asset-formats.md) | 资产格式 | 编辑期 YAML + `.meta`，发布期二进制 + pak | 版本控制友好，发布期加载快 |
| [ADR-0012](../adr/0012-network.md) | 网络 | 不内置 | 缩小范围；对象 GUID 与确定性物理为游戏自行实现留出空间 |
| [ADR-0013](../adr/0013-game-ui.md) | 游戏 UI | 自研 retained-mode UI（Yoga + FreeType + HarfBuzz） | 与编辑器、资产系统、本地化深度集成 |
| [ADR-0014](../adr/0014-audio.md) | 音频 | miniaudio | 单文件、宽松许可、三平台支持 |
| [ADR-0015](../adr/0015-model-import.md) | 模型导入 | fastgltf + ufbx | 体积小、质量高、许可宽松 |
| [ADR-0016](../adr/0016-default-render-pipeline.md) | 3D 默认渲染管线 | Clustered Forward+ | 支持 MSAA 与透明、材质灵活、适合 D3D11 和 TBDR GPU |
| [ADR-0017](../adr/0017-build-artifacts.md) | 构建产物形态 | 独立构建目录：开发版动态库，发布版静态单体 | 热重载需要动态库；发布版需要性能和可移植性 |
| [ADR-0018](../adr/0018-implementation-baseline.md) | 实施基线补充 | 三平台本地验收、Ninja、工程风格、私有 ECS、无损编辑及热重载回滚 | 单人维护，保证兼容性与编辑数据安全 |
