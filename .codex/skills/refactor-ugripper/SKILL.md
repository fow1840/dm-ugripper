---
name: refactor-ugripper
description: 为 UGripper 的重构、模块收口、公共逻辑抽取和路径治理提供工作流；依据当前 overview 与实现确定边界、验证并同步现状文档。
---

# refactor-ugripper

## 工作方式

- 先阅读 `docs/agent/overview.md`，再按任务检查相关实现、CMake 与测试；遵守仓库 AGENTS.md 的图谱导航规则。
- 先说明目标、影响范围、风险、验证方式和执行顺序。仅讨论方案时不修改；用户已明确要求实施时直接在授权范围内推进，无需重复确认。
- 每次围绕一个明确目标落地，区分结构调整与行为变化。以职责更清楚、依赖更窄或副作用更少为成果，不以搬文件为成果。
- 编辑使用 heredoc；保留工作区已有修改。只做与改动相关的验证，未执行或未通过的关键验证需明确说明，不能报告为完成。

## 当前实现约束

- 源码位于 `standalone/` 与 `src/utils/`，测试位于 `test/`；目录及模块职责见 overview 第 12 节。
- 保留 supervisor 多进程模型与现有生命周期。runtime 的领域逻辑放入对应领域模块，避免回流到 `record_runtime.cc`。
- C++ 使用 C++20；Runtime 公开类型使用 `DA::Ugripper`。公共 helper 不反向依赖业务，不为局部重构引入全局调度器或新 IPC 后端。
- 日志后端复用主仓库 `PP::utils`，本仓 `UGripper::utils` 承载适配；不复制另一套 logger 实现。日志 V3 格式与显式错误码规则见 overview。
- camera 的配置、命令拼装、registry 和采集执行分责；保留 overview 第 13 节的 YAML 兼容契约。禁用旧格式属于单独的行为变化。
- 路径调整先核对安装、运行、配置及持久化用途；service、打包路径、进程/通信模型切换需纳入明确任务范围。
- 更新/校准/启动脚本涉及外部状态，修改前核对调用边界与失败恢复。

## 验证与文档

- 纯逻辑用 host-only 单测；脚本用语法与定向验证；ARM 构建、安装和板端测试按改动范围及用户授权执行。
- 更新 `docs/agent/overview.md` 中受影响的现状；有效待办维护在 `docs/todo_list.md`，对外行为变化才更新 `docs/CHANGELOG.md`。
- 不创建或维护重构过程日志、历史阶段计划或重复现状文档。
- 修改代码后运行 `graphify update .`（不支持时使用 `graphify . --update`），只做 AST 增量更新。使用可用的 graphify 专用解释器，不改交叉编译环境依赖。
- 汇报实际改动、验证结果和剩余限制；构建/部署并不因文档整理而自动触发。
