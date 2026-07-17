MCAP 读取说明
==============

本目录提供一个 UGripper MCAP 读取示例脚本：

  read_ugripper_mcap.txt

脚本内容是 Python，只是文件后缀改成了 .txt，方便在会拦截、加密或限制发送 .py 文件的环境里传给数据使用者。运行时仍然用 python3 执行。


1. 安装依赖
-----------

脚本需要 python mcap 包：

  /usr/bin/python3 -m pip install --user mcap

在 UGripper 开发机上建议固定使用 /usr/bin/python3。部分环境里的 python3 可能指向交叉编译解释器，宿主机上无法直接运行。


2. 读取单个 MCAP
----------------

读取 sensor MCAP：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/sensor_left.mcap

只读取 encoder topic，并打印前 10 条样本：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/sensor_left.mcap --topic encoder_left --limit 10

读取 Fays MCAP：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/fays_data_left.mcap --limit 10

只读取 Fays IMU topic：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/fays_data_left.mcap --topic i --limit 10

只读取 Fays camera timestamp topic：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/fays_data_left.mcap --topic c --limit 10


3. 读取整个 episode
-------------------

脚本也可以直接输入 episode 目录：

  /usr/bin/python3 read_ugripper_mcap.txt /mnt/data_disk/<device_sn_lower>/data/episode_<date>_<index> --limit 3

输入 episode 目录时，脚本会自动读取存在的这些文件：

  sensor_left.mcap
  sensor_right.mcap
  fays_data_left.mcap
  fays_data_right.mcap
  ego/sensor.mcap


4. 输出 JSON 报告
----------------

生成机器可读的 JSON 报告：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/sensor_left.mcap --json-out /tmp/sensor_left_report.json

只看 topic 统计，不打印样本：

  /usr/bin/python3 read_ugripper_mcap.txt /tmp/sensor_left.mcap --no-samples

验证当前统一 episode MCAP 使用 ROS 2 profile：

  /usr/bin/python3 read_ugripper_mcap.txt /path/to/episode.mcap --no-samples --expect-profile ros2


5. 当前 payload 格式
--------------------

当前统一 episode MCAP：

  topic: /encoder_left / /encoder_right
  schema: ugripper_msgs/msg/Encoder
  encoding: ROS 2 CDR
  fields: std_msgs/Header header, int32 raw, float32 rad
  payload: 空 frame_id 时固定 28 bytes

header.stamp 与 MCAP logTime 一致，header.frame_id 为空。ugripper_msgs 源码随主包放在 /opt/ugripper/ros2/ugripper_msgs。播放前需要在 ROS 2 workspace 中执行 colcon build，并 source install/setup.bash。离线脚本仍兼容正式发布前的 20-byte direct-stamp CDR 和旧 8-byte binary；新版 ROS 2 消息包只直接反序列化正式 Header CDR。

  topic: /calibration / 条件生成的 /metadata
  schema: std_msgs/msg/String
  encoding: ROS 2 CDR
  data: UTF-8 JSON 文本

sensor_left.mcap / sensor_right.mcap：

  topic: encoder_left / encoder_right
  payload: little-endian int32 raw + float32 rad

fays_data_left.mcap / fays_data_right.mcap：

  topic: i
  payload: little-endian float64[6]
  顺序: gx, gy, gz, ax, ay, az

  topic: c
  payload: little-endian uint32 frame_index


6. ROS 2 / Foxglove 兼容性
--------------------------

当前统一 episode MCAP 使用标准 ros2 profile。安装 rosbag2 MCAP 存储插件后，可以直接查看：

  sudo apt install ros-<distro>-rosbag2-storage-mcap
  ros2 bag info /path/to/episode.mcap

当前 `3.2_mcap` 的图像、CameraInfo、IMU、encoder、calibration 和条件 metadata topic 全部使用 ROS 2 CDR。encoder 是带标准 `header.stamp` 的自定义 `ugripper_msgs/msg/Encoder`，需要先安装对应消息类型；`/calibration` 与 `/metadata` 使用标准 `std_msgs/msg/String`，其 `data` 字段继续承载 JSON。完成消息包安装后，Humble 可直接播放完整 episode。

旧拆分 sensor/Fays MCAP 保留自定义 profile 和紧凑二进制 payload，只建议用本脚本读取。
