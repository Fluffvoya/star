# M5-03 编辑器文档与面板

- 日期：2026-11-28
- 分支：`feat/m5-editor-documents`（基于 `feat/m5-editor-shell`）
- 状态：Linux x64 通过；Windows x64、macOS arm64 未验证

## 本阶段交付

### 场景接口

编辑器读取场景需要几项运行时没有公开的能力：

- `Scene::getObjects()`：按创建顺序返回对象句柄，用于层级面板。
- `Scene::getComponentTypes(handle)` 与 `Scene::findComponentType(type)`：检视器据此枚举组件及其反射元数据。
- `builtinComponentTypes()`：导出 `Transform`、`Camera`、`MeshRenderer`、`Light` 的类型信息。
- `GameObject::setName()`：支持重命名。
- `instantiatePrefabYaml(..., preserve_guids)`：删除撤销时按原 GUID 恢复对象，且不把快照标记成预制体实例。

### EditorCore

- `EditorDocument`：持有一个 `World` 与场景，负责新建、打开（`.scene` YAML）、保存、脏标记、选择集与撤销栈（最多 256 条）。
- 命令：
  - `CreateObjectCommand`：以指定 GUID 创建对象并选中，撤销时销毁。
  - `DeleteObjectCommand`：删除前用 `savePrefabYaml` 快照整棵子树，撤销时按原 GUID 恢复并保持父节点。
  - `RenameObjectCommand`：改名，撤销恢复原名。
  - `SetPropertyCommand`：按属性路径（例如 `position/x`）写入文本值，撤销写回旧值。路径可以穿过嵌套反射结构。
- `resolveProperty` / `formatPropertyText` / `writePropertyText`：检视器与命令共用的属性定位与读写。

### 面板

- 层级：对象列表、选择（Ctrl 多选）、创建与删除按钮。
- 检视器：对象名称（回车提交改名）、GUID、按组件分组的反射属性。标量可编辑（布尔、整数、浮点、嵌套结构逐层展开），枚举显示名称，字符串与容器只读。
- 内容浏览器：扫描 `<项目>/Content` 下的 `.scene`，双击打开；每 2 秒刷新一次，上限 512 个文件。
- 菜单与快捷键：File（New / Save / Quit）、Edit（Undo / Redo，显示命令名）、View（各面板开关）、Ctrl+S / Ctrl+Z / Ctrl+Y。
- 默认停靠：左侧层级，右上检视器，右下统计，下方内容与后台日志。

### StarEditor

- 位置参数作为项目根目录，`--project=<dir>` 等价；`--scene=<file>` 直接打开场景。
- 没有 `--scene` 时新建空场景；打开后自动选中第一个对象，便于检视。

## 验证结果

环境：Linux x64、GCC、llvmpipe（Lavapipe ICD），启用验证层与同步验证。

| 项目 | 结果 |
| --- | --- |
| `linux-dev` 构建 | 通过 |
| CTest | 19/19 通过（新增 `editor-tests`） |
| `documentCreatesObjectsWithUndoAndRedo` | 通过 |
| `documentEditsTransformPropertiesThroughCommands` | 通过：`position/x = 2.5` 写入、撤销、重做 |
| `documentRenamesAndDeletesWithRestore` | 通过：改名撤销；删除撤销后 GUID 不变 |
| `documentSavesAndReopensSceneFiles` | 通过：保存后重新打开，对象名与 `position/y` 保持一致 |
| 界面核对 | 用包含 Root / Child Box / Key Light 的演示项目启动：层级列出三个对象，检视器显示 Transform 的 position/rotation/scale 数值，内容浏览器列出 `Demo.scene`，无验证层报错 |

```bash
cmake --preset linux-dev -DSTAR_SLANGC=$(command -v slangc) -DSTAR_TEST_VULKAN=ON
cmake --build build/linux-dev
VK_ICD_FILENAMES=<lavapipe icd> LD_LIBRARY_PATH=<lavapipe lib> \
    ./build/linux-dev/bin/star-editor <project> --scene=<project>/Content/Scenes/Demo.scene --frames=8
```

## 阶段边界

- 检视器只编辑标量；字符串、Guid、容器与枚举暂为只读。属性元数据（范围、提示）尚未用于控件。
- 内容浏览器只列出 `.scene`，没有资产导入、`.meta` 显示、缩略图与引用查看；项目描述文件（`.starproject`）尚未引入。
- 撤销栈只在内存中，打开新场景即清空；重做栈在命令失败时保留但不会自动裁剪。
- 删除撤销依赖预制体快照，嵌套子树与父引用会一并恢复；跨对象引用（例如 `ObjectRef`）中的 GUID 仍指向原值，但目标对象之外的外部引用不会重映射。
- 中央视口仍为空，渲染、操纵器与 PIE 在后续阶段。
