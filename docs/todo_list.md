# TODO List

这个文件由你和 `todo-hub` 主控 agent 共同维护。你可以直接手改；skill 会按约定解析真实任务。

## 规则

- 默认不会启动全部任务；只会启动你明确点名、`focus = true` 或筛选命中的任务。
- 只有带 `<!-- TODO_HUB_TASK -->` 标记的 TOML 代码块才会被解析成真实任务。
- `slug` 使用英文短横线，用于分支名与 worktree 目录。
- 分支格式固定为：
  - `feature/zhouwu/<slug>`
  - `test/zhouwu/<slug>`
  - `fix/zhouwu/<slug>`

## 新任务模板（复制后记得补 `<!-- TODO_HUB_TASK -->` 标记）

```toml
id = "TASK-001"
title = "示例：补充 USB 音频回归测试"
type = "test"
slug = "usb-audio-regression"
status = "todo"
enabled = false
focus = false
depends_on = []
summary = ""
acceptance = [
  "写第一条验收条件",
  "写第二条验收条件",
]
notes = """
这里写上下文、限制、关注文件、排除范围等。
"""
```

## Active Tasks

在下面追加真实任务块。

## 采集调度优化：本轮实施与验证（进行中）

1. 禁用闲置 USB 耳机按键发现和后台设备扫描。检查 audio_play.py 的按键线程、backend 轮询及 Runtime 启动入口；保留仍需使用的音频控制协议和 Ego 语音链路。禁用处注明风险：耳机缺席时每秒遍历 input 节点并启动多个 udevadm，增加辅助进程调度竞争；恢复功能前须改为事件通知/缓存，不能直接恢复轮询。安装时 udev 规则加载不受影响。
2. 收口 Fays 每秒健康检查中的重复 Python 启动：先合并同轮左右 runtime/calibration 读取，缓存按文件变化失效的标定解析；再由一个常驻标准库 helper 承担解析和原子状态写入，保留 shell 启停/恢复编排。IPC 需有超时、进程退出恢复和过期结果拒绝机制；START/STOP 不等待下一次周期检查，保留全部健康/QC 判定及现有超时阈值。此项涉及生命周期，单独验证后部署。
3. 验证：比较优化前后待机/录制进程启动次数、CPU 占用、编码器实际 TX 间隔与补发密度、Fays 原始视频/IMU gap 和队列丢失；覆盖连续启停、helper 崩溃/超时、状态文件缺失/非法/过期。完成 60s 和 300s 全路录制，核对 MCAP、视频及正式 episode QC。不得新增 metadata 字段、逐帧日志或修改时间戳语义；不能仅以平均 CPU 下降认定 gap 已解决。

预计影响：standalone/UgripperRuntime/audio/audio_play.py、必要的 Runtime 音频启动入口、standalone/FaysStereoRecorder/scripts/run_fays_stereo_daemon.sh 及新增常驻 helper；实现时同步 overview、CHANGELOG 和 graphify。

<!-- TODO_HUB_TASK -->
```toml
id = "TASK-001"
title = "排查其他周期性外部命令并替换高频探测"
type = "fix"
slug = "periodic-process-probes"
status = "todo"
enabled = true
focus = false
summary = "盘点 pactl、设备属性和其他辅助探测；按证据逐项采用缓存、事件通知或常驻服务。"
acceptance = ["记录调用频率和调度关联", "验证热插拔与故障检测时效不退化", "耳机恢复前消除缺席时全节点扫描"]
notes = "udevadm info 是属性查询，不是 USB reset；保留安装阶段规则重载。"
```

<!-- TODO_HUB_TASK -->
```toml
id = "TASK-002"
title = "定位残余采集调度延迟及缺页影响"
type = "test"
slug = "capture-scheduler-latency"
status = "todo"
enabled = true
focus = false
summary = "关联唤醒延迟、IRQ/workqueue、CPU 竞争和缺页，评估核分工及内核抢占方案。"
acceptance = ["区分诊断工具扰动与正式录制现象", "定位缺页对应映射后评估预触页或局部锁页", "比较启停、吞吐和最坏延迟，保留回退方案"]
notes = "不直接全局 mlock、关闭 swap 或启用 RT；现有绑核不能保证消除毫秒级延迟。"
```

<!-- TODO_HUB_TASK -->
```toml
id = "TASK-003"
title = "定位编码器密集请求后持续无回复的硬件边界"
type = "test"
slug = "encoder-burst-stall-root-cause"
status = "todo"
enabled = true
focus = false
summary = "结合 USB 与 UART 线侧抓取区分 CH9344、编码器固件及主机行为。"
acceptance = ["核对请求与回复对应关系", "确认允许补发但限密后的长录制稳定性", "形成可供硬件/固件工程师复现的证据"]
notes = "USB OUT 成功不能证明 UART 已发出或编码器已处理。现有 500us 最小补发间隔作为规避，不宣称根因已修复。"
```

<!-- TODO_HUB_TASK -->
```toml
id = "TASK-004"
title = "区分传感器真实 gap 与批量接收时间戳聚集"
type = "test"
slug = "sensor-gap-timestamp-attribution"
status = "todo"
enabled = true
focus = false
summary = "对齐编码器 TX/RX、批量时间重建、Fays 回调和原始设备时钟证据。"
acceptance = ["分别统计真实请求/回复 gap 与保存时间戳聚集", "明确需要 SDK 介入的原始异常", "任何重建语义调整单独评审且无时间回退"]
notes = "不凭保存数据中小于 0.1ms 的间隔认定真实请求 burst；不掩盖设备原始 gap。"
```


本轮进展：耳机禁用、250µs 串口空读等待、Fays 常驻健康快照已实现并部署240；60秒联合录制、300秒正常Runtime收尾QC及1线程对照已完成。300秒QC成功，但编码器仍有23–24ms保存时间戳间隔、Fays IMU原始最大9.66ms；剩余根因继续按TASK-002/004追踪。状态 JSON 写入与多设备序列号检查中的剩余解释器启动纳入 TASK-001，后续合并时保留错误恢复和状态协议。暂不将包含 MJPEG 解码的 FFmpeg 进程整体迁移至 CPU4–5；独立轻量采集/消费者核分工纳入 TASK-002，根据线程实测负载决定。

300秒验证另见1组触觉相同packet PTS、起录首帧覆盖范围约0.78s；纳入TASK-004区分真实丢帧、批量时间重建与起录覆盖差，不能通过压紧时间轴掩盖。报告：`tmp/board_results/scheduling_optimization_240/REPORT.md`。

实际串口诊断补充：左右各60036请求全部有有效回复；左侧24.829ms峰值对应sleep期望0.931ms、实际24.815ms，前后write仅8/11µs且无缺页增量。TASK-002后续区分定时器唤醒迟到与唤醒后调度排队，不能继续将此峰值归为TTY写等待。

240 内核证据补充（2026-09-30）：定时器按时到期后仍可有约 8.3ms 的 ready→run 延迟，现场 CPU5 运行 kswapd；另有 SensorRecorder 同核线程运行期间约 20.6ms 排队。清理已备份的测试 tmpfs 文件后，一组 60s 的 reclaim/swap 增量归零，但请求峰值仍 9–13ms，不能宣称已解决。部署目录保持原默认值；部署完成后清理本次暂存包，诊断产物拉回核验后清理，避免长期挤占内存。另一类峰值已抓到 `ch9344_tty_write → schedule_timeout` 栈，约 8.6ms 的写调用内等待，唤醒后约 1µs 上 CPU；非阻塞 API 和应用 100ms 重试期限不能中断驱动内部等待。板端驱动 V2.2（2025.04），本机可读 V2.1 源码仅供参考，不作为同版证明。后续应获取匹配源码，关联 USB 完成/发送空通知，验证异步写队列与 write_room 的一致性及超时返回语义，再评估驱动修复；不要用全局 RT 优先级或增加补发密度掩盖问题。证据目录：`tmp/board_results/encoder_latency_new_240/`。

69 六轮 auto/2/1 对照后撤回本轮视频线程限制及新增配置项：2 线程平均 CPU 收益约 1.44%，未显示 encoder gap 改善，1 线程也无稳定收益。继续保留原 FFmpeg 自动线程策略，避免无证据的配置复杂度。

HMI/encoder 共享 CH9344 调度：已复现并修复 HMI pending 查询在25ms静默期的持锁忙转；命令优先级、RGB最新值及保活错峰已实现。240旧版窗口约13k次空读/24.7ms CPU，修复诊断版降至最多1次空读、HMI setter P99降至31–42µs。9组60s联合对照完成，HMI安静时encoder仍有17.7ms请求gap，不能归因全部HMI。350µs一轮有1条稳态额外未匹配请求（仅优先级500µs组也出现过），250/350µs未证明消除长尾，生产补发继续500µs；CH9344内部等待和OS调度仍待定位。最终接收检查计划等待上限20ms，保留25ms禁发。证据与剩余限制见tmp/board_results/hmi_encoder_schedule_240/REPORT.md。
