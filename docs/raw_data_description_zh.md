# Ugripper 原始数据说明

## 1. 适用范围
- 数据格式版本：`1`
- 本说明适用于双设备同步采集场景，包含主设备与从设备。

## 2. 数据存储位置
每台设备的数据独立存储在本机磁盘：

```text
/mnt/data_disk/<device_sn>/data/
```

单次录制目录命名为：

```text
episode_YYYYMMDD_XXXX
```

示例：

```text
/mnt/data_disk/<device_sn>/data/episode_20260326_0001
```

## 3. 单次录制目录内容
每个 `episode` 目录通常包含以下文件：

```text
episode_YYYYMMDD_XXXX/
├── metadata.json
├── calibration.json
├── info.json
├── cam.mkv
├── tact_left.mkv
├── tact_right.mkv
├── sensor_data.mcap
├── fays_stereo_output.mkv
├── fays_data.mcap
├── audio_pre.wav                 # 仅主设备可能生成
└── audio_post.wav                # 仅主设备可能生成
```

## 4. 主从设备行为说明
### 主设备
- 负责录制开始与停止控制。
- 在本机创建 `episode` 目录，并向对端设备发送同步开始/停止指令。
- 可能额外生成 `audio_pre.wav` 和 `audio_post.wav`。

### 从设备
- 在接收到主设备同步指令后开始或停止录制。
- 使用与主设备一致的 `episode` 目录名，便于后续数据配对。
- 不生成 `audio_pre.wav` 或 `audio_post.wav`。
- 在录制结束后，会在 `info.json` 中写入 `paired_master_sn`。

## 5. 角色规则
- `Right` 侧设备固定作为 `Master`。
- `Left` 侧设备在运行时自动判定角色。
- 当网线已连接且主设备 `192.168.1.100` 可达时，`Left` 侧作为 `Slave`。
- 否则，`Left` 侧作为 `Master`。

## 6. 关键文件说明
### `metadata.json`
记录本次录制的运行时元数据，主要包括：
- `device_id`：设备序列号
- `device_side`：设备物理侧别，取值为 `left` 或 `right`
- `device_role`：本次运行角色，取值为 `master` 或 `slave`
- `camera_codec`：视频编码格式，取值为 `h264` 或 `h265`
- `ugripper_version`：采集程序版本
- `ugripper_usb_updater_version`：USB 升级程序版本
- `data_format_version`：数据格式版本

### `calibration.json`
记录本次录制实际使用的标定参数快照，便于后处理时与数据严格对应。

### `info.json`
记录视频相关时间元数据。用于恢复视频帧绝对系统时间的主要字段包括：
- `boot_time_offset`
- `boot_time_offset_us`
- `cam_record_time_offset_us`
- `tact_left_record_time_offset_us`
- `tact_right_record_time_offset_us`

在后处理中，建议优先使用 `*_record_time_offset_us` 作为帧级时间恢复偏移量。

从设备还可能额外写入：
- `paired_master_sn`

若本次录制属于复位重录，`info.json` 中还可能包含：
- `tags: ["reset"]`
- `reset_info`

### `sensor_data.mcap`
该文件以 `MCAP` 格式保存本体侧传感器数据。

包含的 topic：
- `imu_raw`：本体 IMU
- `encoder`：编码器位置

数据载荷定义：
- `imu_raw`：小端序 `float32[10]`
- 字段顺序：`qx`、`qy`、`qz`、`qw`、`gx`、`gy`、`gz`、`ax`、`ay`、`az`
- `encoder`：小端序 `int32 + float32`
- 字段顺序：`raw`、`rad`

时间字段：
- `logTime`：主机系统时间，单位纳秒
- `publishTime`：主机系统时间，单位纳秒

客户侧可通过 `py_script/mcap_sdk.py` 按以下 topic 名读取：
- `imu_raw`
- `encoder`

### `fays_data.mcap`
该文件以 `MCAP` 格式保存 Fays 相关同步元数据。

包含的原始 topic：
- `i`：Fays IMU
- `c`：Fays 视频帧序号与时间映射

数据载荷定义：
- `i`：小端序 `float64[6]`
- 字段顺序：`gx`、`gy`、`gz`、`ax`、`ay`、`az`
- `c`：小端序 `uint32`
- 字段含义：`frameIndex`

时间字段：
- `logTime`：Fays 设备原生时间戳
- `publishTime`：对齐后的系统时间戳

客户侧可通过 `py_script/mcap_sdk.py` 按以下别名读取：
- `fays_imu`，对应原始 topic `i`
- `fays_frame_index`，对应原始 topic `c`

### `fays_stereo_output.mkv`
该文件保存编码后的 Fays 双目视频流。

推荐使用方式：
- 从 `fays_stereo_output.mkv` 中读取视频帧
- 从 `fays_data.mcap` 的 topic `c` 中读取逐帧时间信息
- 使用 `frameIndex` 对应视频帧顺序
- 使用 `publishTime` 作为推荐的系统对齐时间戳

关于 `MCAP` topic 细节、推荐时间列及 `CSV` 导出示例，可参考：
- `docs/mcap_sdk_usage.md`
- `py_script/mcap_sdk.py`

## 7. 传感器时间同步说明
### 跨设备时间基准
- 两台设备通过 `PTP`（`ptp4l` + `phc2sys`）在系统时钟层面进行同步。
- 后台监控服务会持续将 `PTP` 状态写入 `/dev/shm/umi_ptp_status`。
- `NTP` 同步仅在设备空闲且未录制时启用。
- 因此，凡是使用系统时间写入的数据，主从设备之间都可以直接对齐。

### `cam.mkv`、`tact_left.mkv`、`tact_right.mkv`
- 这三路视频文件内部保存的是相对视频 `PTS`。
- 绝对系统时间通过 `info.json` 恢复。
- 相关字段包括：
- `cam_record_time_offset_us`
- `tact_left_record_time_offset_us`
- `tact_right_record_time_offset_us`
- 恢复公式为：

```text
frame_system_time_us = frame_pts_us + <camera>_record_time_offset_us
```

- 其中 `<camera>` 可取 `cam`、`tact_left` 或 `tact_right`。

### `sensor_data.mcap`
- 该文件包含本体侧传感器流：
- `imu_raw`
- `encoder`
- 这两个 topic 的 `logTime` 与 `publishTime` 都直接使用主机系统时间，单位为纳秒。
- 因此，`sensor_data.mcap` 可直接与其他基于系统时间的数据进行对齐，包括主从设备之间的对齐。

### `fays_data.mcap`
- 该文件包含两个 Fays topic：
- `i`：Fays IMU
- `c`：Fays 相机时间戳流
- 这两个 topic 中：
- `logTime` 使用 Fays 原生硬件时间
- `publishTime` 使用对齐后的主机系统时间
- 对齐方式为先利用 IMU 样本计算 `system_time - imu_device_time`，再将该偏移量应用到 Fays 相机时间戳。
- 若用于跨设备或与其他传感器做融合，建议优先使用 `publishTime`。
- 若用于底层调试或分析 Fays 原生时基，建议使用 `logTime`。

### `fays_stereo_output.mkv`
- 该文件保存 Fays 双目视频编码结果。
- 若需要精确时间对齐，建议与 `fays_data.mcap` 中的 topic `c` 结合使用。
- topic `c` 为每一帧编码视频记录一条时间索引，包含：
- `frameIndex`
- `logTime`：Fays 原生时间
- `publishTime`：系统对齐时间
- `fays_stereo_output.mkv` 中的视频帧顺序与 topic `c` 中的 `frameIndex` 顺序一致。
- 因此，推荐通过视频帧顺序匹配 `frameIndex`，再使用 `publishTime` 作为该帧的系统对齐时间。

### 音频文件
- `audio_pre.wav` 和 `audio_post.wav` 是数采阶段用于打标的操作语音文件。
- `audio_pre.wav` 用于录制前的前标注语音。
- `audio_post.wav` 用于录制结束后的后备注语音。
- 这两个文件与当前 `episode` 目录关联保存，但不会写入逐帧或逐采样的同步时间字段。
