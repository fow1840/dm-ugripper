# UGripper Episode MCAP Format

UGripper 本机录制现在每条 episode 只生成一个 `episode.mcap`。录制开始时 `UgripperRuntime` 先启动 `EpisodeMcapRecorder`，真实 Unix socket 放在 `/dev/shm/ugripper/` 下；episode 目录内只在录制期间临时写入 `episode_mcap_socket_path.txt` 供其他 recorder 解析 socket 路径，停录或启动失败后清理。

## 文件

- 必需文件：`episode.mcap`
- 元数据文件：`metadata.json`、`calibration.json`
- 条件文件：`audio_pre.wav`、`audio_post.wav`、`validation_error.log`、可选 `ego/`

## 图像 Topics

所有图像 topic 使用 ROS2 CDR `sensor_msgs/msg/CompressedImage`，`format=h264`。

- `/cam_left/image_compressed`
- `/cam_right/image_compressed`
- `/cam_chest/image_compressed`（启用胸部主摄时）
- `/tcam_left_l/image_compressed`
- `/tcam_left_r/image_compressed`
- `/tcam_right_l/image_compressed`
- `/tcam_right_r/image_compressed`
- `/stereo_left/image_compressed`
- `/stereo_right/image_compressed`

## 相机标定 Topics

每路相机各自写一条 ROS2 CDR `sensor_msgs/msg/CameraInfo`：

- `/<camera>/camera_info`

每路相机各自写一条 JSON 外参 topic：

- `/<camera>/calibration/extrinsics`

`<camera>` 使用 `cam_left`、`cam_right`、`cam_chest`、`tcam_left_l`、`tcam_left_r`、`tcam_right_l`、`tcam_right_r`、`stereo_left`、`stereo_right`。

## 传感器 Topics

- `/encoder_left`
- `/encoder_right`
- `/fays_left/imu`
- `/fays_right/imu`

Fays IMU 使用 ROS2 CDR `sensor_msgs/msg/Imu`。Encoder topic 保持 UGripper binary payload。

## 时间语义

- 主摄与触觉相机：以本路首个输出帧建立 `PTS -> Unix time` offset，后续帧用该 offset 映射到 Unix 时间戳。
- Fays stereo 图像：使用 Fays 硬件 timestamp，通过 IMU timestamp 与主机 Unix time 的 offset 对齐到 Unix 时间轴，不走编码 PTS。
- Fays IMU：使用同一 Fays 硬件时间对齐后的 Unix 时间轴。
- MCAP `logTime`、`publishTime` 与 ROS header stamp 均使用上述 Unix ns。

## 校验口径

停录强校验读取 `episode.mcap` channels/topics：

- 所有启用相机的 `image_compressed`、`camera_info`、`calibration/extrinsics` topic 必须存在且非空。
- `/encoder_left`、`/encoder_right`、`/fays_left/imu`、`/fays_right/imu` 必须存在且非空。
- 所有图像 topic 时间戳必须单调，跨度不能过短，且不能比最长图像 topic 短超过阈值。
