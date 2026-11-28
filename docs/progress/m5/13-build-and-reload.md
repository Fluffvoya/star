# M5-13 编辑器内构建与重载

- 分支：`feat/m5-build-and-reload`
- 验证环境：Linux x64、`linux-dev`、llvmpipe Vulkan。Windows x64、macOS arm64 未验证。

## 交付

- Project Settings 可填写已配置的 CMake + Ninja 构建目录 `BuildDirectory`，以及可选的 `BuildConfiguration`，例如 `Development`。
- Play > Build and Reload 在独立进程执行 `cmake --build`，将标准输出和错误同步到 Console。编辑器在构建过程中持续更新。
- 构建成功后继续执行已验证的模块组预检、快照和重载。构建失败保留当前游戏模块及 PIE。退出请求会在构建结束后进入原有未保存确认流程。

## 验证

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建及软件 Vulkan CTest | 19/19 通过 |
| 构建成功 | 通过编辑器菜单调用临时 Ninja 工程，构建目标发布新版测试模块，Console 记录命令与重载成功 |
| 构建失败 | 构建目标返回退出码 1，Console 记录失败，原模块产物仍一致，未进入重载 |
| 源场景保护 | M5-07、M5-12 自动测试覆盖未保存内容、选择、撤销/重做、未知字段和候选模块失败回滚 |

项目构建目录由项目维护者预先配置。当前验收使用 Linux Ninja，其他平台的进程和动态图形后端还未验证。
