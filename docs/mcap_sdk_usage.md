# MCAP 数据读取说明

本文档说明 `MCAP` 数据文件的读取方式，以及配套脚本 `py_script/mcap_sdk.py` 的用法。

脚本支持：
- 查看文件里有哪些数据
- 查看解码后的内容
- 导出成 `CSV`
- 作为 Python 类接口供外部脚本调用

## 1. 安装依赖

需要：
- `Python 3.7` 或更高版本
- Python 包：`mcap`

安装命令：

```bash
python3 -m pip install mcap==1.3.1
```

安装完成后，可先检查脚本帮助信息：

```bash
python3 py_script/mcap_sdk.py --help
```

## 2. 使用说明

### 2.1 查看文件内容

```bash
python3 py_script/mcap_sdk.py info /path/to/fays_data.mcap
```

这个命令会显示：
- 文件里有哪些数据
- 每类数据有多少条
- 开始时间和结束时间

### 2.2 查看几条数据

查看 `fays_data.mcap` 里的 IMU：

```bash
python3 py_script/mcap_sdk.py show /path/to/fays_data.mcap --topic fays_imu --limit 5
```

查看 `fays_data.mcap` 里的视频帧序号：

```bash
python3 py_script/mcap_sdk.py show /path/to/fays_data.mcap --topic fays_frame_index --limit 5
```

查看 `sensor_data.mcap` 里的编码器：

```bash
python3 py_script/mcap_sdk.py show /path/to/sensor_data.mcap --topic encoder --limit 5
```

### 2.3 导出成 CSV

导出整个文件：

```bash
python3 py_script/mcap_sdk.py export-csv /path/to/fays_data.mcap --output-dir /tmp/fays_csv
```

导出指定数据：

```bash
python3 py_script/mcap_sdk.py export-csv /path/to/fays_data.mcap" \
  --topic fays_imu \
  --topic fays_frame_index \
  --output-dir /tmp/fays_csv

python3 py_script/mcap_sdk.py export-csv /path/to/sensor_data.mcap \
  --topic imu_raw \
  --topic encoder \
  --output-dir /tmp/sensor_csv
```

导出后会按数据类型分别生成 `CSV`，例如：
- `fays_data__fays_imu.csv`
- `fays_data__fays_frame_index.csv`
- `sensor_data__imu_raw.csv`
- `sensor_data__encoder.csv`

## 3. 文件说明

设备当前会产出两类 `MCAP` 文件：

| 文件名 | 内容 |
| --- | --- |
| `fays_data.mcap` | Fays 相机相关时间戳和 Fays IMU |
| `sensor_data.mcap` | 本体 IMU 和编码器 |

## 4. `fays_data.mcap` 说明

### 4.1 `fays_imu`（原始 topic: `i`）

表示：
- Fays IMU 数据

导出后的主要列：
- `gx`, `gy`, `gz`：角速度
- `ax`, `ay`, `az`：加速度
- `log_time_ns`
- `publish_time_ns`
- `recommended_time_ns`

### 4.2 `fays_frame_index`（原始 topic: `c`）

表示：
- Fays 视频的帧序号和对应时间

导出后的主要列：
- `frame_index`：视频帧序号
- `log_time_ns`
- `publish_time_ns`
- `recommended_time_ns`

说明：
- `frame_index` 从 `0` 开始
- `frame_index = 0` 表示视频第 1 帧
- `frame_index = 1` 表示视频第 2 帧
- 可以用 `frame_index` 去对应 `fays_stereo_output.mkv` 的视频帧

## 5. `sensor_data.mcap` 说明

### 5.1 `imu_raw`

表示：
- 本体 IMU 数据

导出后的主要列：
- `qx`, `qy`, `qz`, `qw`：姿态四元数
- `gx`, `gy`, `gz`：角速度
- `ax`, `ay`, `az`：加速度
- `log_time_ns`
- `publish_time_ns`
- `recommended_time_ns`

### 5.2 `encoder`（原始 topic: `encoder`）

表示：
- 编码器位置数据

导出后的主要列：
- `raw`：原始值
- `rad`：弧度值
- `log_time_ns`
- `publish_time_ns`
- `recommended_time_ns`

## 6. 时间列怎么选

导出的 `CSV` 里会有 3 列时间：
- `log_time_ns`
- `publish_time_ns`
- `recommended_time_ns`

如果只是正常使用，直接优先使用：
- `recommended_time_ns`

简单理解：
- `log_time_ns`：原始记录时间
- `publish_time_ns`：对齐后的时间
- `recommended_time_ns`：脚本推荐你使用的时间列

通常建议：
- 看 `fays_data.mcap` 时，优先用 `recommended_time_ns`
- 看 `sensor_data.mcap` 时，也优先用 `recommended_time_ns`

## 7. 常见用法

### 7.1 找视频某一帧对应的时间

先导出：

```bash
python3 py_script/mcap_sdk.py export-csv /path/to/fays_data.mcap --topic fays_frame_index --output-dir /tmp/fays_csv
```

然后查看 `fays_data__fays_frame_index.csv`：
- `frame_index` 是帧号
- `recommended_time_ns` 是建议使用的时间

### 7.2 找某一时刻的 Fays IMU 数据

先导出：

```bash
python3 py_script/mcap_sdk.py export-csv /path/to/fays_data.mcap --topic fays_imu --output-dir /tmp/fays_csv
```

然后查看 `fays_data__fays_imu.csv`：
- `gx`, `gy`, `gz` 是角速度
- `ax`, `ay`, `az` 是加速度
- 时间优先看 `recommended_time_ns`

### 7.3 查看本体 IMU 和编码器

```bash
python3 py_script/mcap_sdk.py export-csv /path/to/sensor_data.mcap --output-dir /tmp/sensor_csv
```

导出后：
- `sensor_data__imu_raw.csv` 是本体 IMU
- `sensor_data__encoder.csv` 是编码器

## 8. Python 类接口

脚本可直接在外部 Python 脚本中导入使用。

可用类：
- `McapParser`：通用读取类
- `FaysMcapReader`：专门读取 `fays_data.mcap`
- `SensorMcapReader`：专门读取 `sensor_data.mcap`

### 8.1 通用读取

```python
from py_script.mcap_sdk import McapParser

reader = McapParser("/path/to/fays_data.mcap")

info = reader.get_info()
topics = reader.list_topics()
rows = reader.read_topic("fays_imu", limit=10)
```

### 8.2 读取 Fays IMU

```python
from py_script.mcap_sdk import FaysMcapReader

reader = FaysMcapReader("/path/to/fays_data.mcap")

imu_rows = reader.read_imu(limit=5)
for row in imu_rows:
    print(row["recommended_time_ns"], row["gx"], row["gy"], row["gz"])
```

### 8.3 建立视频帧号到时间的映射

```python
from py_script.mcap_sdk import FaysMcapReader

reader = FaysMcapReader("/path/to/fays_data.mcap")
frame_to_time = reader.build_frame_index_map()

print(frame_to_time[0])
```

### 8.4 读取本体 IMU 和编码器

```python
from py_script.mcap_sdk import SensorMcapReader

reader = SensorMcapReader("/path/to/sensor_data.mcap")

imu_rows = reader.read_imu(limit=5)
encoder_rows = reader.read_encoder(limit=5)
```

## 9. 注意事项

- `fays_frame_index` 的原始 topic 名是 `c`，其中最重要的是 `frame_index`，它用来对应视频帧。
- 做时间对齐时，优先使用 `recommended_time_ns`。
- 如果只想用 Excel 或表格软件分析，建议先导出 `CSV` 再查看。
