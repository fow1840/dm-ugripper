# UGripper 原始数据使用指南

> **不推荐用户直接处理设备原始数据。请优先使用平台处理后的数据集。**
>
> 原始数据包含多种时间域、设备标定和异常状态，直接解码或按帧号拼接可能产生时间错位及错误样本。仅在平台数据不能满足调试、研究或自定义处理需求时，参考本文读取原始数据，并自行完成格式、同步和质量检查。平台数据的具体处理范围以对应数据集说明为准。

适用范围：UGripper 2.1.12 系列及当前开发版本；以 `metadata.json` 中的软件版本、数据版本和实际文件为准。本文更新于 2026-09-21。历史数据不能仅按文件名套用当前格式；开发分支功能不代表全部 2.1.12 安装包都支持。

本文用于组内同步原始数据格式与时间对齐约定。阅读顺序：先确认产物与版本，再读取视频/MCAP，最后统一时间轴并检查质量。本文中的数据路径均为示例，请替换为实际路径。文内代码可直接复制运行，不依赖项目仓库或随附脚本。

## 1. 数据目录与文件

设备数据通常位于 `/mnt/data_disk/<设备SN小写>/data/episode_YYYYMMDD_NNNN/`。录制中的目录带 `-temp` 后缀；请使用停录、收尾及后台校验完成后的最终目录。最终目录存在不等于质量合格。

```text
episode_YYYYMMDD_NNNN/
  metadata.json             # 背包元数据与质量结果
  calibration.json          # 背包相机/传感器标定
  cam_left.mkv              # 左、右夹爪主摄
  cam_right.mkv
  tcam_left_l.mkv           # 四路触觉相机
  tcam_left_r.mkv
  tcam_right_l.mkv
  tcam_right_r.mkv
  stereo_left.mkv           # 左、右双目模组的视频
  stereo_right.mkv
  sensor_left.mcap          # 左、右编码器等传感器消息
  sensor_right.mcap
  fays_data_left.mcap       # 双目帧时间戳及 Fays IMU
  fays_data_right.mcap
  ego/                     # 可选；有独立 metadata/calibration
    metadata.json
    calibration.json
    rgb.mp4
    tracking.mp4
    ctrl.mp4
    sensor.mcap
    audio.m4a
    ego_sync.json
```

- `+nostereo` 包不生成双目视频与 Fays MCAP；可选胸部主摄为 `cam_chest.mkv`。
- `audio_pre.wav`、`audio_post.wav`、`validation_error.log` 按录制条件或异常情况生成。
- 持续授时版本可能保存 `ego/time_sync_samples.jsonl`；毫秒表测试可能保存 `stopwatch_timing.json`，它们不是每条数据的必需文件。
- `left/right` 是设备侧别；触觉文件末尾的 `l/r` 是该侧夹爪上的传感器位置，不是另一只夹爪。
- 双目视频当前通常为 640×800，上下拼接两个 640×400 图像。每个模组都有两目，左右模组不能当成一对双目。图像次序与 cam0/cam1 对应应结合该 SDK/固件及标定确认。

## 2. 元数据与标定

### 2.1 metadata.json

| 字段 | 含义与用法 |
| --- | --- |
| `data_version` | 数据协议版本；当前为 3.3，历史有 3.0/3.1/3.2 |
| `software_version`、`hardware_version` | 用于定位软件行为与设备差异 |
| `device_sn`、`hardware_list` | 背包与各传感器身份；用于匹配标定 |
| `camera_codec` | 视频编码配置，实际编码仍应通过视频流确认 |
| `require_files` | 当前录制配置的必需文件；Ego 文件可能不在其中，需另查 |
| `video_details` | 每路视频名称、时间起点、时长等；按 `name` 匹配，不依赖数组顺序 |
| `video_details[].start_offset_us` | **PTS=0 对应的绝对 Unix 微秒**，不是相对 episode 起点 |
| `collection_duration_s` | 采集时长汇总；不代表每一路覆盖同一时间区间 |
| `task_start_s`、`task_stop_s` | 人工任务标记，Unix 秒；0 或缺失不作为有效时刻 |
| `boot_time_s` | 系统开机时刻，Unix 秒；不是需要加到所有流上的时间偏移 |
| `quality_check_status` | `success` / `fail` / 空；空值不代表成功 |
| `quality_check_err_type` | 主失败原因，更多证据看日志 |
| `tactile_damage_sn_list` | 触觉损坏检测结果 SN，可能由后台校验更新 |

metadata 3.3 在 data_version 后包含 13 个版本字符串（新增 rk3588_firmware_version 位于 ego_firmware_version 后、hardware_list 前，来自 /home/user/version.info，复用现有版本缓存；历史 3.3 缺少该新增字段仍兼容）；ego_bound（布尔值）紧邻 ego_software_version 之前，hardware_list 末项为 ego_sn（未绑定为空）。主摄固件版本取 USB iSerial；胸摄未启用、+nostereo 双目固件写 "disabled"；未绑定 Ego 两个版本为空。起录前所有启用设备版本必须已知，缺项拒绝创建 episode，待机继续限频重试；起录冻结版本和绑定 SN，停录只消费内存副本。其余 metadata 字段、顺序、类型及 calibration 格式保持不变。Ego 拉回并完成远端大小校验与清理后，在本地 ego/metadata.json 写 related_episode_name 和 related_episode_uuid，分别等于 UMI episode_name（不带 -temp）和 data_uuid。 范本：`.codex/skills/validate-episode-data/references/metadata-v3.3.json`。

### 2.2 calibration.json

当前标定格式为 3.1，历史可能为 3.0；它与 metadata 的 `data_version=3.3` 是两套独立版本号。包含相机模型、分辨率、内参、畸变、外参及标定状态等；主摄外参可来自夹爪存储的眼眼/手眼标定，Fays 使用模组标定。

使用前核对设备 SN、图像尺寸、相机模型、畸变模型及标定有效状态。默认结构参数或单位矩阵不等于完成有效标定。变换矩阵必须确认源/目标坐标系、单位、左右乘约定；不要根据字段名自行推断。SDK 的 `timeshift_cam*_to_imu0` 也不能未经核对就叠加到已对齐的落盘时间上。

夹爪二进制读写协议不等同于 episode JSON 格式，读取原始数据时应以随数据保存的标定 JSON 为准。Ego 使用自己的 `ego/calibration.json`，不能套用背包相机标定。

## 3. 视频怎么读取

主摄、触觉和双目以 MKV 保存，编码可为 H.264/H.265；Ego 使用其应用生成的视频。先探测实际流信息，不硬编码分辨率、帧率或颜色格式。

```bash
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,width,height,pix_fmt,time_base,avg_frame_rate \
  -of json /path/to/episode/cam_left.mkv

ffprobe -v error -select_streams v:0 -show_frames \
  -show_entries frame=pts,best_effort_timestamp,pict_type \
  -of json /path/to/episode/cam_left.mkv > /tmp/cam_left_frames.json
```

使用 FFmpeg/PyAV 等解码；逐帧保存对应 PTS 和 `time_base`，不要在导出时自动补帧或按固定 FPS 重建时间。解码帧按显示时间关联，不能把存在重排序的视频直接按压缩包数组下标对应图像。遇到缺失 PTS 应标记异常，不静默用帧号代替。

双目源图像为灰度，但编码文件可能以 YUV420 表示；解码成 RGB 不会增加原始图像精度。当前 NV12 编码使用有限范围亮度，分析像素值时注意解码器的色彩范围转换。视频帧率字段是播放参数，不能单独证明没有采集丢帧。

## 4. MCAP 格式与读取

这些文件是**通用 MCAP 容器，不是 ROS 2 bag**。`ros2 bag info` 或 Foxglove 的 ROS 导入失败，不足以说明文件损坏。可视化前可能需要转成对应 schema。

### 4.1 背包自定义二进制 payload

| 文件 / topic | 字节数 | 小端 payload | 含义 |
| --- | --- | --- | --- |
| `sensor_*.mcap` / `encoder_left` 或 `encoder_right` | 8 | `<if` | int32 原始计数 + float32 弧度 |
| `fays_data_*.mcap` / `i` | 48 | `<6d` | float64：gx, gy, gz, ax, ay, az |
| `fays_data_*.mcap` / `c` | 4 | `<I` | uint32 视频帧索引 frameIndex |

MCAP 消息头的 `publishTime` / `logTime` 不在上述 payload 中。Python API 分别叫 `publish_time` / `log_time`。即使 schema 描述使用 JSON 文本，channel 的 `message_encoding=binary` 仍表示 payload 不是 JSON。

编码器弧度可乘 `180/π` 转为角度，不把原始计数直接当作角度。Fays IMU 数值沿用 SDK 输出，轴向和单位需按所用 SDK/固件协议确认；不要未经确认换算为度/秒或乘重力加速度。未知 topic 应保留 schema、encoding 与原始字节，不能套用上表。Ego MCAP 由外部应用产生，按其 schema 解码。

### 4.2 准备读取环境

在独立的数据分析 Python 环境中安装 MCAP 和压缩依赖。视频探测需要系统中可用的 FFmpeg/ffprobe；不要修改设备运行环境或交叉编译环境。

```bash
python -m pip install mcap lz4 zstandard
```

下面的代码直接使用第三方 MCAP 库，不依赖 UGripper 仓库。替换文件路径后即可运行。示例面向当前背包二进制格式，Ego 数据需按其消息 schema 单独解析。

### 4.3 最小读取示例

```python
import struct
from collections import Counter
from mcap.reader import make_reader

# 可替换为 sensor_left/right.mcap 或 fays_data_left/right.mcap。
path = "/path/to/episode/fays_data_left.mcap"
counts = Counter()
previous = {}

with open(path, "rb") as source:
    messages = make_reader(source).iter_messages(log_time_order=False)
    for schema, channel, msg in messages:
        topic = channel.topic
        counts[topic] += 1
        t_ns = msg.publish_time
        if topic in previous and t_ns <= previous[topic]:
            print("重复或回退:", topic, previous[topic], t_ns)
        previous[topic] = t_ns
        decoded = None
        if channel.message_encoding == "binary":
            if topic in ("encoder_left", "encoder_right"):
                if len(msg.data) != 8:
                    raise ValueError("encoder payload 长度异常")
                raw, rad = struct.unpack("<if", msg.data)
                decoded = {"raw": raw, "rad": rad}
            elif topic == "c":
                if len(msg.data) != 4:
                    raise ValueError("camera payload 长度异常")
                frame_index, = struct.unpack("<I", msg.data)
                decoded = {"frame_index": frame_index}
            elif topic == "i":
                if len(msg.data) != 48:
                    raise ValueError("IMU payload 长度异常")
                values = struct.unpack("<6d", msg.data)
                decoded = dict(zip(
                    ("gx", "gy", "gz", "ax", "ay", "az"), values))
        # 仅打印每个 topic 的前三条，避免大文件输出过多。
        if counts[topic] <= 3:
            print(topic, "publish_ns=", t_ns,
                  "log_ns=", msg.log_time, "sample=", decoded,
                  "encoding=", channel.message_encoding,
                  "schema=", schema.name if schema else None)

print("各 topic 消息数量:", dict(counts))
```

保留文件内顺序可以检查真实回退；先按时间排序会掩盖此类异常。处理大文件应流式读取，不必将全部图像和传感器消息放入内存。

## 5. 如何统一时间轴

单位速查：`task_*_s/boot_time_s` 为秒，`start_offset_us` 为微秒，MCAP 消息头时间为纳秒；`PTS` 是 tick，必须乘 `time_base` 才是秒。

统一目标是 Unix 纳秒整数 `t_ns`。Unix 时间本身不随时区改变；中国时区只影响显示。使用整数或有理数换算，避免大时间戳经浮点数转换损失精度。

| 数据流 | Unix 纳秒换算 |
| --- | --- |
| 主摄、触觉视频 | `start_offset_us × 1000 + PTS × time_base × 10^9` |
| 本机 encoder 等 Unix 域 MCAP 消息 | 直接取消息头 `publish_time` |
| 双目视频 | 与同侧 `c` 消息的 frameIndex 对应，取其 `publish_time` |
| Fays IMU | 直接取消息头 `publish_time` |
| Ego 单调时钟 topic | 用 Ego metadata/head_pose 锚点换算，见下文 |
| Ego 已为 Unix 的 metainfo 等 topic | 直接使用，不能再次加偏移 |

### 5.1 主摄与触觉

主摄录制端已完成固件时钟映射及必要修正，使用最终落盘 PTS；不再用原始 SEI 替换，也不重复加历史补偿。

```python
from fractions import Fraction

start_offset_us = 1780000000000000  # 实际取本路 metadata
pts = 40                          # 实际取解码帧
base = Fraction(1, 1000)           # 实际取该帧/视频 time_base
relative_ns = Fraction(pts) * base * 1_000_000_000
t_ns = start_offset_us * 1000 + round(relative_ns)
# 1780000000040000000 ns
```

如果已经读取 `pts_time` 秒值，不要再乘 time_base。不能把每路首帧分别归零后声称它们已同步。

### 5.2 双目与 IMU

双目 MKV 使用固定帧率播放轴，设备 gap 可能被压紧。**不能用 MKV PTS 或帧号/FPS 重建双目真实采集时间**。必须读取同侧 Fays MCAP 的 `c` 消息，核对帧数、索引连续性与顺序后关联解码图像。

`frameIndex` 是录制端每条视频从 0 开始的连续写入索引，不是相机硬件帧号。索引连续也可能存在采集丢帧，需检查相邻 `c.log_time` 的时间间隔。

视频帧数与 `c` 消息数量不一致时，不能从缺失位置之后继续按数组下标强行配对；先定位异常，无法恢复对应关系的区段标记不可用。Fays `log_time` 是设备原始时间，用于分析设备侧 gap；`publish_time` 才是背包统一后的时间。双目 camera 固定补偿已落到 `publish_time`，不要再次应用。

### 5.3 Ego

新版 Ego 通过 USB ACM 串口持续与背包系统时钟对齐；旧版使用录制前 ADB 校时。离线状态下，两者时间一致不代表已经对准标准 UTC。同步状态、offset、RTT 描述协议测量质量，不是曝光时刻精度证明。

已支持的 Ego 数据中，`head_pose/controller_pose/imu` 可能使用设备单调时间。令：

```text
A_us = ego/metadata.json 中 head_pose_details[name=head_pose].start_offset_us
H0_ns = head_pose 首条有效消息的原始 publish_time
Delta_ns = A_us × 1000 - H0_ns
Ego Unix 时间 = 原始 publish_time + Delta_ns
```

只对确认共用该单调时钟域的 topic 使用此偏移；`*_metainfo` 等已是 Unix 的消息不加。缺锚点、应用版本不匹配或时间域不明时应停止自动换算，不能仅凭时间戳大小猜测。

Ego 视频需要其自身 metadata 中对应视频的时间起点和时间基定义，不能使用背包根目录的视频起点，也不能默认所有 Ego 视频与 head_pose 同一起点。应用版本不同可能改变字段与格式；无法确认视频时间定义时，保留原始 PTS 并请求该版本的格式说明。

### 5.4 对齐多个流

1. 分别建立逐帧/逐消息 Unix 时间，保留原始序号及时间戳。
2. 检查重复、回退、gap、首末覆盖和视频/MCAP 对应关系。
3. 共同区间取 `[max(各流首时刻), min(各流末时刻)]`；无交集就不能生成完整多模态样本。
4. 选择主摄时刻或统一采样网格，图像取最近帧并限制误差；连续量按需插值，姿态使用符合其定义的四元数插值。长 gap 不跨越插值。
5. 输出有效性标记和每路匹配误差。需要相对时间时，最后统一减去同一个 `t0_ns`。

统一时间域不等于硬件曝光同步。滚动快门、曝光时长、固件时间定义和传输延迟仍可能带来误差；物理精度须通过毫秒表或共同事件验证。首次启动错峰与持续时间漂移应分开统计。

### 5.5 对齐结果最少保留什么

建议每条对齐样本保留：共同参考时间 `t_ns`、各路原始帧/消息索引、各路原始时间、匹配误差 `delta_ms=(流时间−参考时间)/10^6` 和有效性标记。正值表示该路晚于参考时间。这样发现错位时可以追溯，而不必重新猜测匹配关系。

检查 gap 时使用真实采集时间：主摄/触觉用落盘 PTS，双目用同侧 `c.log_time`。分别统计启动阶段和稳定采集阶段，不把平均 FPS 正常或 frameIndex 连续当作无丢帧。Ego 首帧较晚说明启动错峰，不能直接作为持续授时误差。
