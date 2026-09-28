# Ugripper 错误码对照表
错误码格式固定为 `UGR-责任域-根因序号`；当前 `error_type` 与新码的映射键是“`error_type` + 根因判定条件”。侧

`UGR-00-01` 固定表示“根因未确认”：已经观察到异常结果，但当前证据不足以确认责任域或失败源。映射到该码的 `missing_file`、`finalize_error`、`frame_loss` 等是结果或现象，不被解释为根因。

## 错误码分类

| 责任域 | 分类 |
|---|---|
| `00` | 公共编排、进程和控制通道 |
| `10` | 数据盘与持久化 |
| `20` | 主相机 |
| `21` | 触觉相机 |
| `22` | Fays 双目与 IMU |
| `30` | Encoder / Sensor |
| `40` | 夹爪 HMI |
| `50` | 音频和 USB HID |
| `60` | Episode 校验器自身 |
| `70` | 配置与标定导入 |
| `80` | 系统动作与 USB 恢复 |
| `81` | USB 升级与安装 |
| `82` | Ego 联动 |
| `83` | 时间同步 |

## 当前映射

| 新错误码 | 当前 error_type | 根因判定条件 |
|---|---|---|
| `UGR-00-01` | `unknown` | 任意 |
| `UGR-00-01` | `runtime_error` | 无法进一步确认根因 |
| `UGR-00-01` | `missing_file` | 无已确认上游根因 |
| `UGR-00-01` | `finalize_error` | 无已确认上游根因 |
| `UGR-00-01` | `calibration_error` | 无法确认失败源 |
| `UGR-00-01` | `device_disconnected` | 无法确认设备责任域 |
| `UGR-00-01` | `collection_duration_too_short` | 无已确认上游根因 |
| `UGR-00-01` | `frame_loss` | 无已确认上游根因 |
| `UGR-00-01` | `critical_devices_missing` | 无法确认设备责任域 |
| `UGR-00-01` | `stereo_not_ready` | 无法确认失败源 |
| `UGR-00-01` | `main_camera_v4l2_startup_failure` | 无法确认失败阶段 |
| `UGR-00-01` | `main_camera_short_stream` | 无主相机源停滞证据 |
| `UGR-00-05` | `-` | Camera、Sensor 或其他录制 worker fork、exec、启动失败或异常退出 |
| `UGR-00-06` | `-` | 录制 worker 停止、kill、wait 或 reap 失败 |
| `UGR-10-01` | `disk_mount_lost` | 任意 |
| `UGR-10-02` | `disk_not_writable` | 任意 |
| `UGR-10-03` | `disk_full` | 任意 |
| `UGR-10-03` | `disk_space_unavailable` | 任意 |
| `UGR-20-01` | `device_disconnected` | 主相机设备缺失或断开 |
| `UGR-20-01` | `critical_devices_missing` | 主相机缺失 |
| `UGR-20-02` | `main_camera_v4l2_startup_failure` | open、format、buffer 或 `STREAMON` 启动失败 |
| `UGR-20-03` | `frame_loss` | 主相机已确认 I/O 停滞或失败 |
| `UGR-20-03` | `main_camera_v4l2_startup_failure` | poll、DQBUF 或 QBUF 已确认失败 |
| `UGR-20-03` | `main_camera_v4l2_kernel_hang` | 任意 |
| `UGR-20-03` | `main_camera_short_stream` | 有主相机源停滞证据 |
| `UGR-20-05` | `main_camera_v4l2_startup_failure` | GStreamer、appsrc 或 bus 媒体管线失败 |
| `UGR-20-06` | `calibration_error` | 主相机身份或标定读取失败 |
| `UGR-21-01` | `device_disconnected` | 触觉相机设备缺失或断开 |
| `UGR-21-01` | `critical_devices_missing` | 触觉相机缺失 |
| `UGR-21-03` | `calibration_error` | 触觉相机 serial、reference 或 baseline 处理失败 |
| `UGR-22-01` | `device_disconnected` | Fays stereo/IMU 节点缺失、remap 或断开 |
| `UGR-22-01` | `critical_devices_missing` | Fays stereo/IMU 缺失 |
| `UGR-22-02` | `stereo_daemon_not_running` | 任意 |
| `UGR-22-02` | `stereo_status_missing` | 任意 |
| `UGR-22-02` | `stereo_status_invalid` | 任意 |
| `UGR-22-02` | `stereo_not_ready` | daemon 或 status 源不可用 |
| `UGR-22-03` | `calibration_error` | Fays SDK 配置、handle、device info 或 calibration 失败 |
| `UGR-22-03` | `stereo_not_ready` | SDK 或初始化配置失败 |
| `UGR-22-04` | `frame_loss` | Fays 已确认停止出帧或返回无效帧 |
| `UGR-22-04` | `stereo_frame_loss` | 本条录制单侧单次连续缺失超过 3 帧；日志记录缺失数，延后 1 秒停录；metadata 只使用现有质量字段 |
| `UGR-22-04` | `stereo_not_ready` | 已确认无帧或无效帧 |
| `UGR-22-06` | `stereo_control_failed` | Fays FIFO、START/STOP 或 session 控制失败 |
| `UGR-22-06` | `stereo_not_ready` | FIFO 或 session 控制失败 |
| `UGR-22-07` | `-` | Fays 视频 ffmpeg pipe、fork、写入或退出失败 |
| `UGR-22-08` | `-` | Fays MCAP 打开、写入或 finalize 失败 |
| `UGR-30-01` | `device_disconnected` | Encoder 设备断开 |
| `UGR-30-01` | `critical_devices_missing` | Encoder 缺失 |
| `UGR-30-02` | `-` | Encoder 串口初始化、打开、重连或无响应 |
| `UGR-30-02` | `encoder_init_failed` | 任一侧编码器初始化失败，或启动后 5 秒内未确认左右有效样本；取消起录 |
| `UGR-30-03` | `-` | Encoder 响应或协议数据无效；仅 E/F 输出携带此码 |
| `UGR-30-04` | `-` | Sensor MCAP 打开、写入或 finalize 失败 |
| `UGR-30-05` | `-` | Sensor 写队列持续积压；仅 E/F 输出携带此码 |
| `UGR-30-06` | `-` | Encoder 清零事务失败 |
| `UGR-30-07` | `encoder_stream_stale` | 任一侧编码器连续 3 秒无有效样本或采集中断连；自动停录并保留失败原因 |
| `UGR-30-08` | `encoder_tail_missing` | 编码器 topic/消息缺失、尾部不可读、缺少视频参考时间，或末帧落后同侧视频末尾超过 1 秒 |
| `UGR-40-01` | `device_disconnected` | HMI 设备或端口断开 |
| `UGR-40-01` | `critical_devices_missing` | HMI 设备或端口缺失 |
| `UGR-40-01` | `hmi_all_disconnected` | 任意 |
| `UGR-40-01` | `hmi_ports_disconnected` | 任意 |
| `UGR-40-01` | `hmi_input_disconnected` | 任意 |
| `UGR-40-02` | `hmi_input_inactive` | 任意 |
| `UGR-40-02` | `hmi_ports_inactive` | 任意 |
| `UGR-40-03` | `-` | HMI transport 或事务协调失败 |
| `UGR-40-04` | `-` | HMI 协议响应无效或固件拒绝请求 |
| `UGR-40-05` | `-` | HMI serial number 事务或内容无效 |
| `UGR-40-06` | `calibration_error` | HMI 标定事务或 payload 无效 |
| `UGR-50-01` | `-` | 音频运行资源、控制通道或 backend 不可用 |
| `UGR-50-02` | `-` | 音频播放或输出失败 |
| `UGR-50-03` | `-` | 音频录制或输入失败 |
| `UGR-50-04` | `-` | USB 耳机 HID 输入不可用；warning 不因该码提升为 error |
| `UGR-50-05` | `-` | 音频制品处理、移动或提交失败 |
| `UGR-70-01` | `-` | 启动或执行所需 YAML、配置、脚本或程序缺失/无效 |
| `UGR-70-03` | `calibration_error` | 标定导入或装配失败 |
| `UGR-80-01` | `-` | shutdown、umount 等系统动作请求、等待或结果确认失败 |
| `UGR-80-02` | `-` | USB 恢复前置检查失败 |
| `UGR-80-03` | `-` | USB 恢复命令、脚本或运行资源不可用 |
| `UGR-80-04` | `-` | USB 电源控制通信、执行或结果验证失败 |
| `UGR-80-05` | `-` | USB 恢复完成后目标设备仍不健康 |
| `UGR-81-01` | `-` | 升级资源、U 盘或目标挂载不可用 |
| `UGR-81-02` | `-` | 升级包或包元数据无效 |
| `UGR-81-03` | `-` | 包安装或 postinst 失败 |
| `UGR-81-04` | `-` | 安装前 service 停止或资源释放失败 |
| `UGR-81-05` | `-` | 安装后 service 或升级流程恢复失败 |
| `UGR-82-01` | `ego_disconnected` | 已绑定或要求联动的 Ego/ADB 不可用 |
| `UGR-82-01` | `-` | Ego 联动要求的 ADB 或设备不可用 |
| `UGR-82-02` | `-` | Ego 时间同步失败 |
| `UGR-82-03` | `-` | Ego session 启动或发现失败 |
| `UGR-82-04` | `ego_pull_incomplete` | Ego 数据拉取、读写或同步不完整 |
| `UGR-82-04` | `-` | Ego 数据拉取、读写或同步失败 |
| `UGR-82-05` | `-` | Ego stop、finalize 或清理失败 |
| `UGR-83-01` | `-` | 时间同步所需网络不可用 |
| `UGR-83-02` | `-` | NTP 或时间同步 backend 执行失败 |

## 后续错误兼容

- `error_type` 为 `-` 的行表示该基线已有明确失败分支、但原日志没有 `error_type`；实现必须依据表中的直接失败条件显式传入错误码。
- `60` 域当前没有可映射的 Runtime `error_type` 或已知失败分支；该责任域保留，不删除，也不跨域复用编号。
- 后续新增错误先归入已有责任域，再分配尚未发布的该域编号；已发布的编号不改义、不复用。
- 每个责任域的 `UGR-XX-90` 到 `UGR-XX-99` 预留给 V3 之后新增、且无法由现有码加上下文表达的根因。使用预留编号时，必须补充本表的分类和映射行。
- `collection_duration_too_short`、`frame_loss` 在已有上游根因时沿用上游根因码；它们本身不创建新的根因码。
- `operator_marked_failed` 是人工质量标记，不生成系统根因码。
- `ERROR_1` 到 `ERROR_5` 是 HMI LED 状态，不是 `error_type`，不参与本表，也不直接映射错误码。

## 工程备注

`UGR-00-01` 的定义是“未知错误原因”或“根因未确认”。仅当错误路径已经观察到异常，但无法依据当前 `error_type`、设备责任域和直接失败证据确认具体根因时使用。

`missing_file`、`finalize_error`、`collection_duration_too_short`、`frame_loss` 等现象不能单独被当作根因；存在已确认上游根因时，必须输出上游根因码，而不是 `UGR-00-01`。不得根据日志文本中的 `error`、`failed` 等字样猜测并替换为具体错误码。
