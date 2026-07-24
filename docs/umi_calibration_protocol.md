# UMI HMI SN And Force Calibration Protocol

本文档描述夹爪 HMI 的 SN 与力传感器标定存储格式。相机、双目和 IMU 标定保存在各自相机模组中，不再写入 HMI calibration payload。

## 1. 兼容性约定

- 串口参数：`115200 8N1`。
- 独立 SN 读写命令、响应格式和 `32-byte` 字段保持不变。
- SN 使用 `1..32` 个可打印 ASCII 字符，尾部补 `0x00`；当前现场通常使用 16 字符 SN。
- calibration 传输仍固定为 `1024 byte`，按 `64 x 16B` 写入。
- UCAL header 仍是原来的 `32-byte` 布局；力标定格式版本为 `0x00020000`。
- payload 内再次保存完全相同的 `32-byte` SN，用于防止标定串爪。

## 2. Payload 布局

所有多字节字段在 payload 内使用 little-endian。

| Offset | Size | Block |
| --- | ---: | --- |
| `0x000` | `32` | 原 UCAL header |
| `0x020` | `64` | 力传感器标定块 |
| `0x060` | `928` | 保留，写零 |

有效内容为 `96 byte`，传输层仍补满 `1024 byte`。

### 2.1 UCAL header @ `0x000`

| Offset | Size | 字段 | 值 |
| --- | ---: | --- | --- |
| `0x000` | 4 | `magic` | `UCAL` |
| `0x004` | 4 | `dataFormatVersion` | `0x00020000` |
| `0x008` | 2 | `payloadSize` | `96` |
| `0x00A` | 2 | `headerSize` | `32` |
| `0x00C` | 4 | `validFields` | bit0 表示力标定有效块存在 |
| `0x010` | 16 | `reserved` | 写零 |

### 2.2 Force block @ `0x020`

| Offset | Size | 字段 | 说明 |
| --- | ---: | --- | --- |
| `0x020` | 4 | `magic` | `FCAL` |
| `0x024` | 2 | `blockVersion` | `1` |
| `0x026` | 2 | `blockSize` | `64` |
| `0x028` | 32 | `serialNumber` | 与独立 HMI SN 完全相同的编码 |
| `0x048` | 4 | `zeroRaw` | 空载 signed int32 raw |
| `0x04C` | 4 | `referenceRaw` | 标准砝码 signed int32 raw |
| `0x050` | 4 | `referenceMassMg` | 标准质量，1 kg 为 `1000000` mg |
| `0x054` | 4 | `flags` | bit0 表示两点标定有效 |
| `0x058` | 4 | `calibratedAtUnixSeconds` | 标定时间 |
| `0x05C` | 4 | `crc32` | 前 60 byte 的 IEEE CRC32 |

## 3. 换算规则

`0x30` 继续上报大端 signed int32 raw，不改变现有报文语义。主机按 payload 换算质量：

```text
delta_raw = reference_raw - zero_raw
mass_mg = (raw - zero_raw) * reference_mass_mg / delta_raw
```

中间计算必须使用 signed int64。`delta_raw` 自带方向，不能额外翻转符号。

## 4. 有效性判断

力标定只有同时满足以下条件才可使用：

1. UCAL/FCAL magic、版本、size 正确。
2. header bit0 和 force flags bit0 均置位。
3. `referenceMassMg > 0` 且 `referenceRaw != zeroRaw`。
4. CRC32 正确。
5. payload 内 `serialNumber` 与独立 SN 读取命令返回的 32-byte 字段完全一致。

SN 不匹配时只允许继续读取 raw，不能输出标定后的质量。改写 SN 不需要改动现有 SN 协议，但会自然使旧力标定失效。

## 5. 写入流程

1. 使用现有命令读取夹爪 SN。
2. 采集空载 `zeroRaw`。
3. 施加已知质量，采集 `referenceRaw`。
4. 构造 `UCAL 0x00020000`，把当前 SN 原样编码进 force block。
5. 沿用现有 calibration 写入命令写满 `64 x 16B`。
6. 读回 payload，并校验 header、CRC、标定点和 SN 绑定。

`GripperHmiTool` 提供直接入口：

```bash
GripperHmiTool --port /dev/left_gripper \
  --write-force-calib \
  --zero-raw -5000 \
  --reference-raw 495000 \
  --reference-mass-mg 1000000
```

离线生成相同 payload：

```bash
python3 auto_calibration/generate_gripper_calibration_bin.py \
  --sn DAGY12267600014A \
  --zero-raw -5000 \
  --reference-raw 495000 \
  --reference-mass-mg 1000000 \
  --output-bin /tmp/force_calibration.bin
```

`GripperHmiTool --write-calib-bin` 与纯 Python `gripper_hmi_toolkit.py --write-calib-bin`
均只接受合法的 `0x00020000` FCAL payload。写入前会读取目标夹爪 SN，并要求 payload
中的 32-byte SN、CRC 和两点参数全部有效，旧格式或 SN 不匹配时拒绝写入。

## 6. 遗留格式

`0x00010000` 是旧相机/双目/IMU HMI payload。新软件只把它识别为 legacy，不再生成或写入；首次完成力标定时会由新格式整体替换。
