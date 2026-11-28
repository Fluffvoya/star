# M5-10 项目描述与设置

- 分支：`feat/m5-project-settings`
- 验证环境：Linux x64、`linux-dev`、llvmpipe Vulkan。Windows x64、macOS arm64 未验证。

## 交付

- StarEditor 可接收项目目录或 `.starproject`。从描述文件解析启动场景和游戏模块，路径相对于描述文件所在目录。`--scene` 和 `--game-module` 可覆盖对应设置。
- File > Project Settings 编辑项目名称、引擎版本、启动场景和游戏模块。场景、项目设置共用同目录临时写入再替换的流程。保存时通过反射序列化并保留未知设置。

```yaml
StarFile: Project
Version: 1
Name: Example
EngineVersion: "0.1.0"
Modules: []
StartupScene: "Content/Scenes/Demo.scene"
```

## 验证

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建及软件 Vulkan CTest | 19/19 通过 |
| 项目描述往返 | 解析项目相对路径，拒绝错误的场景扩展名，保存后保留未知字段 |
| 启动验证 | 直接传入 `.starproject`，在未传 `--scene` 的情况下加载其启动场景并绘制 12 帧 |

当前编辑器可启动一个游戏模块。项目描述保留模块列表字段，当前面板提供单模块设置。渲染、物理、输入、音频及打包设置还未接入此面板。
