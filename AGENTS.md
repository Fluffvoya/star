# Star Engine：代理规则索引

这是仓库唯一的规则入口。具体规则集中在 `agent-rules/`；`@` 后是相对于仓库根目录的路径，按任务需要主动打开对应文件。

- 开始任何新任务前：读取 @agent-rules/git.md，检查工作区并创建本次任务的新分支。
- 修改实现、构建配置或测试：读取 @agent-rules/development.md。
- 编写或格式化代码及代码示例：读取 @agent-rules/style.md。
- 修改 `engine/` 下的源码、着色器、配置或内置资产：读取 @agent-rules/engine.md。
- 修改项目文档：读取 @agent-rules/documentation.md；处理 ADR 时另外读取 @agent-rules/adr.md。
