# UGripper Log Studio

给测试工程师使用的独立、离线日志阅读工具。复制整个 `log_viewer` 目录，用近期 Chrome / Edge 打开 `index.html` 即可，不依赖 UGripper、Python、Node 或网络服务。

## 使用

1. 将设备上的 `/tmp/umi_sys_<sn>_<date>.log` 或 `/mnt/data_disk/logs/` 中的日志复制到电脑。
2. 点击「导入日志」或拖入一份文件。新文件读取成功后替换当前日志，并重置筛选、分页和统计；同时拖入多份文件会提示重新选择，读取失败保留当前日志。顶部显示当前文件名。
3. 勾选 INFO / WARNING / ERROR，组合错误码、关键词和时间范围筛选。错误码下拉框自动列出当前文件全部日志中的错误码及次数，选择后精确匹配；重置筛选恢复为全部错误码。FATAL / CRITICAL 归入 ERROR；未识别等级保留为 UNKNOWN。
4. 选择「仅 restore」聚焦恢复流程，或「隐藏 restore」排除过程噪声。展开时间线、点击事件或日志可查看同文件前后 5 行原文。
5. 「导出当前筛选」导出所有匹配行的原始内容，不仅是当前页。导出保持当前文件原始行顺序。

## 统计口径

总览、restore、错误码和重复消息统计基于当前文件全部日志，不随筛选变化。日志明细展示筛选命中数。

| 指标 | 事件标记 |
| --- | --- |
| 请求 | `restore usb requested` |
| 执行次数 | `restore usb start`，每次重试单独计一次 |
| 命令完成 | `restore usb command completed`，仅表示脚本成功退出 |
| 恢复成功 | `restore usb recovered`，健康状态恢复证据 |
| 命令失败 | `restore usb command failed` / `failed to start` |
| 重试耗尽 | `restore usb failed after max attempts` |
| 等待 / 等待取消 | `restore usb pending wait` / `pending cleared` |
| 超时 | `restore usb symlinks timeout` / `ready timeout` |

次数按明确日志事件计数，不根据 attempt 最大值估算，不把等待和请求当执行。实际执行不意味着 GPIO 一定复位成功。缺少 recovered 时无法断言设备未恢复或已恢复；日志截断会影响统计。时间线保留 cause、evidence、side、attempt 等原始证据，最多展示最后 1000 个事件；完整记录可从「仅 restore」查看和导出。

## 格式与容量

优先识别当前 `[Unix秒.微秒][tid ...][I/W/E][函数 行号]` 格式，兼容常见方括号/单词等级及 ISO 日期。时间按电脑本地时区显示到毫秒；导出保留原始微秒。无日期的旧日志不推测日期，启用时间过滤后排除未知时间行。多行内容按物理行保存，不猜测缺失等级。

文件以 UTF-8 流式解码，列表每页 200 行；全部解析记录仍驻留内存，建议单文件不超过 50 MB，超大日志按天拆分。重复消息按去除当前标准日志头后的完整正文精确聚合，不会把不同 evidence 合并。日志内容作为文本展示，不执行 HTML。

开发验证：`node tools/log_viewer/test_parser.cjs`。运行工具本身不需要 Node。

## 中文问题提示

带错误码的日志下方直接显示一句简短中文，例如“数据盘无法写入”“主相机启动失败”“USB 复位后设备仍异常”。说明依据错误码对照表编写，不展示英文类型或映射条件。未知根因只提示“检测到异常，具体原因尚未确认”，不会猜测具体设备故障。上下文和导出保留原文。

维护说明：中文文案位于 `error_descriptions.json`，依据 `docs/error code/Ugripper错误码对照表.md` 维护。更新后运行 `node tools/log_viewer/sync_error_codes.cjs`，检查错误码覆盖并生成离线 `error_codes.js`。工具使用者无需查表，也无需安装 Node。
