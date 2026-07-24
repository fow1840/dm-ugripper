---
type: "query"
date: "2026-07-22T06:18:54.949870+00:00"
question: "ugripper和ego的校时阈值"
contributor: "graphify"
source_nodes: ["sync_ego_time()", "measure_time_offsets()", "best_offset_ms()"]
---

# Q: ugripper和ego的校时阈值

## Answer

当前实现没有配置 UGripper 与 Ego 的最大允许校时偏差阈值。默认前、中、后各采样3次，选RTT最小样本，先设置一次Ego时间再按测得偏差二次修正；只要set-time成功且最终偏差可读取即标记ok，不按偏差绝对值判失败。Ego起录状态等待超时为5秒，但这不是校时偏差阈值。

## Source Nodes

- sync_ego_time()
- measure_time_offsets()
- best_offset_ms()