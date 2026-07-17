# UGripper Episode MCAP Format

UGripper 本机录制每条 episode 最终只生成两个主要产物：`<DEVICE_SN>_<YYYYMMDD_HHMMSS>.mcap` 和 `metadata.json`。MCAP Header 使用标准 `ros2` profile；MCAP 文件名中的 `DEVICE_SN` 来自 `/etc/environment`，时间戳使用起录时刻并精确到秒。录制期间 `UgripperRuntime` 会临时启动 `EpisodeMcapRecorder`，真实 Unix socket 放在 `/dev/shm/ugripper/` 下；episode 目录内的 socket marker、`calibration.json` 和 MCAP stats sidecar 只服务录制/停录收尾，最终目录不保留。

## 文件

- 必需文件：`<DEVICE_SN>_<YYYYMMDD_HHMMSS>.mcap`
- 条件文件：`audio_pre.wav`、`audio_post.wav`、`validation_error.log`
- 不再生成最终文件：`calibration.json`

原 `calibration.json` 的完整内容在统一 MCAP socket 就绪后、相机/传感器真正起录前写入 MCAP topic `/calibration`。JSON 文本封装在 ROS 2 CDR `std_msgs/msg/String.data` 中。`metadata.json` 保留在 episode 目录，作为最终校验结果载体；普通停录收尾阶段不再向 MCAP 追加 `/metadata`。若数采员在停录后通过左手单键长按把上一条 episode 补标为失败，运行时会离线改写最终 MCAP，在首个 chunk 中增加同样使用 String CDR 的 `/metadata` 标记消息。

## 图像 Topics

所有图像 topic 使用 ROS2 CDR `sensor_msgs/msg/CompressedImage`。主摄与触觉相机当前写 `format=h264`；Fays stereo 图像写 `format=h265`。

- `/cam_left/image_compressed`
- `/cam_right/image_compressed`
- `/cam_chest/image_compressed`（启用胸部主摄时）
- `/tcam_left_l/image_compressed`
- `/tcam_left_r/image_compressed`
- `/tcam_right_l/image_compressed`
- `/tcam_right_r/image_compressed`
- `/stereo_left/image_compressed`（`format=h265`）
- `/stereo_right/image_compressed`（`format=h265`）

`header.stamp` 与 MCAP `logTime` 一致，均为 Unix ns 对齐后的时间。

## 相机 CameraInfo

每路相机各写一条标准 ROS2 CDR `sensor_msgs/msg/CameraInfo`：

- `/<camera>/camera_info`

`<camera>` 使用 `cam_left`、`cam_right`、`cam_chest`、`tcam_left_l`、`tcam_left_r`、`tcam_right_l`、`tcam_right_r`、`stereo_left`、`stereo_right`。

`CameraInfo` 字段来源：

- `header.stamp`：该路相机首帧 Unix 对齐时间。
- `header.frame_id`：相机 topic 名。
- `height` / `width`：标定中的 `shape` 或运行时配置分辨率。
- `distortion_model`：标定中的畸变模型字符串，例如 `plumb_bob`、`equidistant` 或设备标定给出的模型名。
- `d`：畸变参数数组，对应 `distortion_coeffs`。
- `k`：3x3 内参矩阵，对应 `intrinsics` 中的 `fx/fy/cx/cy`。
- `r`：3x3 rectification matrix；未提供时写单位矩阵。
- `p`：3x4 projection matrix；未提供时由内参填充。
- `binning_x` / `binning_y` / `roi`：当前写默认值。

## Calibration Topic

完整标定写入单条 ROS 2 String topic：

- `/calibration`

该 topic 在录制开始阶段写入，schema name 为 `std_msgs/msg/String`、schema encoding 为 `ros2msg`、channel encoding 为 `cdr`。`String.data` 是原 episode `calibration.json` 的完整 JSON 内容，channel metadata 使用 `content_type=application/json` 与 `ugripper_json_kind=calibration` 标记业务语义。外参不再拆成 `/<camera>/calibration/extrinsics`；读取外参时先反序列化 String，再从 JSON 的 `observation.images` 下获取对应相机条目，例如 stereo 的 `cam0/cam1/extrinsics`。

String CDR 由静态 vendoring 的 eProsima Fast-CDR 1.0.23 生成；板端不依赖 ROS 2 runtime。CDR payload 相对原始 UTF-8 JSON 增加 4-byte encapsulation、4-byte 字符串长度和 1-byte 结尾空字符。

## Metadata File

完整元数据保留在 episode 目录下的 `metadata.json`，停录强校验和离线使用都以该文件为准。

停录后人工失败补标会把上一条 episode 的 `metadata.json` 更新为 `quality_check_status=fail`、`quality_check_err_type=operator_marked_failed`。同一次补标还会重写最终 MCAP：先完整读取原 MCAP，再生成临时 MCAP，并把一个 `std_msgs/msg/String` CDR `/metadata` 标记消息写为新文件的首条消息，使其位于第一个 chunk；该消息的 `log_time/publish_time` 使用原 MCAP 最早消息时间，避免按时间排序时落到 episode 尾部。`String.data` JSON 至少包含 `quality_check_status`、`quality_check_err_type`、`source=left_hand_single_long_press`、`episode_name` 与代表真实补标时刻的 `marked_time_us`。临时 MCAP 写完后会 flush、rename 覆盖原文件，并再次 flush MCAP 文件和 episode 目录。

## 传感器 Topics

- `/encoder_left`
- `/encoder_right`
- `/imu_left`
- `/imu_right`

`/imu_left` 与 `/imu_right` 使用标准 ROS2 CDR `sensor_msgs/msg/Imu`。orientation 当前无有效姿态估计，写 `w=1` 且 `orientation_covariance[0] = -1` 标记姿态不可用；angular velocity 与 linear acceleration 使用 Fays IMU 原始数据。

Encoder topic 使用 ROS2 CDR `ugripper_msgs/msg/Encoder`：

```text
std_msgs/Header header
int32 raw
float32 rad
```

`header.stamp` 与 MCAP `logTime` 一致，`header.frame_id` 为空，左右侧由 topic 名表达；`raw` 是编码器原始计数，`rad` 是换算后的弧度值。空 `frame_id` 时 CDR payload 固定为 28 bytes；3.2 正式发布前的 direct-stamp 试录 payload 为 20 bytes，旧版紧凑 binary payload 为 8 bytes。消息包源码随 ugripper 主包安装到 `/opt/ugripper/ros2/ugripper_msgs`，ROS 2 主机需先在 colcon workspace 中构建并 source 该包，之后才能发布或解码 encoder topic。离线解析脚本兼容三种 payload；ROS 2 消息包只保证直接反序列化正式 Header 格式。

从 `data_version=3.2_mcap` 开始，完整 MCAP 的所有 channel 都使用 ROS 2 CDR。安装并 source `ugripper_msgs` 后，Humble 可直接执行 `ros2 bag info` 和 `ros2 bag play`；`/calibration` 与条件生成的 `/metadata` 可按 `std_msgs/msg/String` 订阅，再解析 `data` 内的 JSON。

## 时间语义

- 主摄与触觉相机：以本路首个输出帧建立 `PTS -> Unix time` offset，后续帧用该 offset 映射到 Unix 时间戳。
- Fays stereo 图像：Fays 硬件 timestamp 通过 IMU 首帧硬件时间与 Unix time 的 offset 对齐到 Unix 时间轴，不走编码 PTS。
- Fays IMU：使用同一 Fays 硬件时间对齐后的 Unix 时间轴。
- Fays 图像和 IMU 的 MCAP `logTime`：Unix 对齐后的时间。
- Fays 图像和 IMU 的 MCAP `publishTime`：Fays 相机/IMU 独立硬件时间轴原始值。
- Fays 图像和 IMU 的 ROS `header.stamp`：与 `logTime` 一致，便于 ROS/Foxglove 直接按 Unix 时间播放。
- 主摄、触觉、encoder：`logTime` 和 `publishTime` 均使用 Unix/system 时间。

## 校验口径

停录强校验读取最终 MCAP 的 channels/topics：

- `/calibration` 必须存在且非空。
- 所有启用相机的 `image_compressed`、`camera_info` topic 必须存在且非空。
- `/encoder_left`、`/encoder_right`、`/imu_left`、`/imu_right` 必须存在且非空。
- 所有图像 topic 时间戳必须单调，跨度不能过短，且不能比最长图像 topic 短超过阈值。
