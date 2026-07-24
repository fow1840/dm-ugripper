---
type: "query"
date: "2026-07-22T06:25:33.000865+00:00"
question: "为什么UGripper和Ego校时读取偏差却没有阈值判定"
contributor: "graphify"
source_nodes: ["sync_ego_time()", "best_offset_ms()", "run_start()"]
---

# Q: 为什么UGripper和Ego校时读取偏差却没有阈值判定

## Answer

当前代码确实读取了校时后的post_best_offset_ms，但该值只用于记录与观测；mid_best_offset_ms用于第二次闭环修正。最终status条件仅检查set-time命令无错误且final_offset可读取，没有检查abs(final_offset)是否小于阈值。因此这是当前实现缺少残余偏差验收条件，而不是没有读取偏差。提交历史没有说明具体阈值被有意删除；结合Ego sidecar失败不阻塞主录制的设计，只能确认其采用best-effort策略，但即使不阻塞，也仍可按阈值标记error/warning。

## Source Nodes

- sync_ego_time()
- best_offset_ms()
- run_start()