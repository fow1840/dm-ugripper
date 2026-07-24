---
type: "query"
date: "2026-07-23T06:37:17.922143+00:00"
question: "现在读取到的UGripper和Ego偏移量在哪里看"
contributor: "graphify"
source_nodes: ["sync_ego_time()", "egoWorkerArgs()", "compact_time_sync()"]
---

# Q: 现在读取到的UGripper和Ego偏移量在哪里看

## Answer

偏移量写入每条episode的ego/ego_sync.json，在time_sync对象中查看pre_best_offset_ms、mid_best_offset_ms和post_best_offset_ms。最终路径通常为/mnt/data_disk/<device_sn_lower>/data/episode_YYYYMMDD_NNNN/ego/ego_sync.json；录制或收尾期间目录名带-temp。单位为毫秒，offset=Ego时间-UGripper时间，因此正值表示Ego快，负值表示Ego慢。当前主运行日志不会直接打印这些数值。

## Source Nodes

- sync_ego_time()
- egoWorkerArgs()
- compact_time_sync()