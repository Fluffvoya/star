# M3-01 场景与对象存储阶段报告

- 日期：2026-11-26
- 分支：`feat/m3-scene-foundation`
- 状态：Linux x64 本地验证完成

## 本阶段交付

- 新增 `Star::Scene` 运行时模块。World 持有 Scene，Scene 通过带代数的 SlotMap 持有 GameObject，通过 GUID 哈希表查找对象。
- 对象句柄绑定到所属 Scene。删除后的句柄、其他 Scene 的句柄以及已卸载 Scene 的句柄均无法解析。
- 支持生成 GUID、创建指定 GUID 的对象，以及拒绝无效或重复 GUID。销毁命令在提交边界生效，重复入队同一对象只执行一次销毁。
- Scene 私有 EnTT registry 维护实体与对象句柄的映射。EnTT 3.14.0 由 vcpkg 清单固定，公共头文件只引用 Star SDK 与 C++ 标准库。

## 验证结果

- Linux x64 Development 共享库构建使用上游 EnTT 3.14.0 的 CMake 包。CTest 12 项全部通过，包含 Scene、公共头独立编译、序列化和生成反射测试。
- Linux x64 Development 单体静态构建通过 Scene 专项测试及 Scene 公共头独立编译。
- Scene 专项覆盖对象身份、GUID 索引、无效和重复 GUID、延迟销毁、槽位复用及场景归属校验。
- Windows x64、macOS arm64 和 vcpkg 工具链构建尚未验证。本次 Linux 构建启用了 Null RHI。

## 后续阶段

后续阶段交付组件注册与查询、层级与变换传播、生命周期与调度、场景 YAML 与预制体，以及独立游戏模块的加载与卸载验收。
