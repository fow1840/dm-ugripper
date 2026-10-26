# 双目短帧手动复现与监测

脚本 `fays_short_frame_monitor.py` 只读 usbmon，不改配置、不重启服务、不起停录制。使用 Python 3 标准库。适用于当前 Fays 640×800 格式，默认完整 USB 帧 513292 字节（含 12 字节 UVC 头）；其他格式需指定 `--expected-bytes`。

1. 保持 Ego、左右夹爪正常连接，确认设备 Ready、Ego 已绑定且授时成功。保持双目 buffer=16，IMU buffer=64；不要通过拔插 USB 或修改双目 buffer 人为制造另一种故障。
2. 将脚本传至板端（仓库环境源码有透明加密，使用 cat 管道）：

   ```sh
   cat test/scripts/fays_short_frame_monitor.py | ssh ubuntu@192.168.2.240 'cat > /tmp/fays_short_frame_monitor.py'
   ssh -t ubuntu@192.168.2.240 'sudo python3 /tmp/fays_short_frame_monitor.py --duration 180'
   ```

3. 看到 `READY` 后，正常按键起录，录制 120 秒后正常停录。脚本 180 秒后退出。若录制还没结束，监测退出不会停止录制。可重复数轮，每轮生成独立目录；这是偶发问题的观察复现，不能保证每轮出现。
4. 查看控制台每路 `frames/anomalies` 和输出目录 `summary.json`。`capture_valid=true` 且某帧 `bytes < 513292` 为短帧证据。`anomalies` 也包括超长帧、USB 错误和 UVC ERR，需按明细区分；0 次仅代表该窗口未捕获。若采集丢日志或缺少某路数据，脚本报 INCOMPLETE，不能将统计用于确认设备短帧。
5. 保存 `raw.jsonl`、`summary.json`、本轮 episode 和内核日志，结合 MCAP 双目时间戳判断录制影响。采集包含起停前后预览帧，必须按时间对齐，不能把所有异常都算作录制丢帧。`wall` 是主机读日志时刻，只用于粗对齐；`kernel_us` 是 usbmon 微秒计数，约 4096 秒回绕，不可直接当 Unix 时间或跨回绕相减。

默认自动从 `/dev/stereo_left`、`/dev/stereo_right` 查 USB bus/device，视频端点默认为 bulk IN 1。特殊拓扑可重复指定 `--target 6:13:1 --target 8:13:1`，设备重插后须重新确认编号。需要 root 和已启用的 `/sys/kernel/debug/usb/usbmon/`；路径不存在时脚本失败，不自动修改内核状态。运行时长限制 1–1800 秒、原始日志上限 256 MiB；Ctrl-C 只结束监测。首次/末次不完整观察边界不参与统计。

离线重算：

```sh
python3 test/scripts/fays_short_frame_monitor.py --analyze /path/to/raw.jsonl
```

已用 2026-10-20 的实际 USB 记录验证：左侧 3079 个闭合帧中 1 个 484356 字节，右侧 3080 帧无异常。240 板端 5 秒冒烟：左右各 124 帧、无异常、text_lost 不增加。这不是新增一次完整录制复现结果。

本次故障直接证据：完整帧应为 513292 字节，实际少 28936 字节，V4L2 未交付该帧，随后一帧还延迟，MCAP 出现 120ms 缺口。同期 USB 完成状态均为 0，未见视频 URB 断供、应用队列丢帧。设备组帧/FIFO 或控制器背压仍需设备侧日志区分，不能仅凭此证明具体固件缺陷。增大接收 buffer 无法补回缺失的帧内容。
