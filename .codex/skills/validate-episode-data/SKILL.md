---
name: validate-episode-data
description: 为 ugripper 项目执行 episode 数据深度校验。用于用户在录完一条新数据后，指定“校验 episode / 检查这条录制数据 / 验证视频和 sensor 是否同步 / 看有没有 gap 或异常 / 校验最新一条数据”等请求时：先阅读 docs/agent/overview.md 理解当前 episode 结构与时间语义，再对指定 episode 目录执行只读分析；若用户说“校验最新一条数据”，默认对用户给出的数据父目录执行 `--latest` 选择最新 `episode_*`；按 metadata 的普通或 `+nostereo` profile 优先检查全部视频、全部已发现 sensor topic 与 Fays IMU/camera 是否处于同一 Unix 时间域并用表格展示所有模态的起止相对偏移（ms）与 gap 简要统计，再检查关键文件、metadata 3.3/历史 3.0、3.1及3.2、calibration 3.1/历史 3.0、逐帧 gap、主摄时间戳异常、sensor 同步与覆盖率，并给出结论、证据和建议。
---

# validate-episode-data

## 固定执行顺序

1. 先阅读 `docs/agent/overview.md`，确认当前 episode 产物、`metadata.json` 3.3 版本和时间字段语义、`calibration.json` 3.1 外参结构，以及普通或 `+nostereo` profile 的视频 / sensor / Fays 输出结构。
2. 找到用户指定的 episode 目录；如果用户只给了父目录，可在其中定位最近一条 `episode_*`。
   - 若用户直接说“校验最新一条数据”，默认将这句话解释为“对指定数据父目录下最新的 `episode_*` 做校验”。
3. 无需等待确认，直接执行只读分析脚本：
   - `bash test/scripts/validate_episode_data.sh <episode_dir>`
   - 若只给数据父目录，优先使用：`bash test/scripts/validate_episode_data.sh <parent_dir> --latest`
   - 入口会依次选择 `uv`、项目 `.venv` 和系统 Python；缺少 MCAP 依赖导致全局首帧校验无法完成时必须判失败。
4. 若用户只关注某一类问题，可按需加参数缩小范围：
   - `--latest`：当传入的是父目录时，自动选择最新的 `episode_*`。
   - `--skip-video-packets`：仅在用户明确缩小范围或同意快速检查时使用；跳过后视频 gap 必须标记“未检查”，不能据此给出全模态 gap 通过。
   - `--json`：输出结构化结果，便于后续保存或机器消费。
   - `--top N`：调整输出的最大 gap 样本数。
5. 输出时必须区分：
   - 总体结论：`PASS / WARN / FAIL`
   - 具体异常项
   - 支撑证据
   - 建议下一步

## 默认检查范围

默认至少覆盖以下项目：

- 按 `software_version` 的 `+nostereo` 构建标记检查 episode 关键文件是否存在、非空、可读，并用 `require_files` / `video_details` 交叉验证 profile 一致性。
- `data_version=3.3` 为当前 metadata schema；`3.0/3.1/3.2` 作为历史兼容版本允许分析，未知版本告警。
- `task_start_s/task_stop_s` 允许同时缺失或为 `0.0`；只有开始标记时按无回环数据处理，有结束无开始或结束不晚于开始时判失败。
- `metadata.json` 结构与关键时间字段是否完整，是否发生 schema 漂移；`video_details[].start_offset_us` 必须是视频 `PTS=0` 对应的 Unix 微秒 offset，不允许使用以最早视频为 `0` 的 episode 相对值。
- `calibration.json` 独立按 `format_version` 校验：当前 `3.1`、历史 `3.0`，未知版本失败。检查图像和左右 IMU 标定项；3.1 要求独立主摄外参状态，以及左右主摄各两项有限数值 4×4 刚体矩阵，胸部不增加外参。`uncalibrated` 的合法默认矩阵允许通过格式检查，不等于已完成光学标定。
- `metadata.json` / `info.json` / `calibration.json` 是否出现未登记字段、缺失字段或字段类型变化，防止 JSON 格式被偷偷改动。
- 若存在 `validation_error.log`，是否说明当前 episode 在停录时已有失败记录。
- 所有启用视频（含胸摄）以及实际存在的 Ego 视频、音频容器是否可读，是否存在明显时间戳异常。
- 所有模态的起止系统时间是否对齐，是否有异常长尾 / 短尾；必须展示下文两张完整表格。
- 各视频流是否存在明显逐帧 / 逐包 gap、重复时间戳、回退时间戳。
- 主摄专项时间戳扫描是否提示 `backward_dts / decode_non_monotonic_dts / decode_error / ref_missing`。
- 容器 `duration` 与包级 `span` 是否自洽。
- 左右 main / stereo 成对视频的起止时间是否对齐。
- 全部启用视频、本机 MCAP 中实际发现的全部 sensor topic、默认包左右 Fays IMU/camera，以及存在时 `ego/sensor.mcap` 的全部 topic 必须放入同一张 Unix 首帧对齐表；Ego 单调时钟 topic 按 `head_pose_details.start_offset_us` 计算固定 offset，已是 Unix 时间的 topic 不重复换算；始终列出各流相对最早流的误差，最大误差达到 `1000ms` 时判失败，未超阈值的数据仍保留该表供后续优化。
- `stereo_session` 中的首帧 / 结束时间与顶层 offset 语义是否自洽。
- 左右 `sensor_*.mcap` 是否可读，topic 是否齐全。
- Fays camera `publishTime` 首末跨度、真实 gap/回退、frameIndex、MCAP camera count 与 stereo MKV packet count 是否一致；stereo 对齐使用 `publishTime`，设备侧 gap 使用 `logTime`，不得用压紧后的 MKV PTS 替代真实采集时间。
- 全部模态逐流检查 gap 并简要统计，包含全部视频、音频、本机 / Ego sensor topic、左右 Fays IMU/camera；Fays 分别统计 device/logTime 与 Unix/publishTime。保留 encoder gap、长时间中断和样本数异常检查。
- 左右 sensor 同类型 topic 是否明显不同步。
- sensor 覆盖时长是否明显短于视频。
- encoder / imu 尾部是否明显早停。
- 是否存在明显的分组错峰起录模式，例如 `stereo -> tactile -> main`。

## 必须输出的两张表

无论 PASS / WARN / FAIL，都展示全部已启用或已发现的数据流；不得只列异常流。原有文件、schema、标定、时间域、首帧阈值、主摄专项、配对同步、覆盖率和尾部等校验全部保留。

1. **所有模态起止相对偏移表**：每行一个视频、音频或 sensor / Fays topic，列出流名、样本 / 帧数、起始相对偏移 ms、结束相对偏移 ms、时间映射方式及必要说明。
   - 起始基准 = 所有可映射流中最早有效首样本的 Unix 时间；偏移为正表示该流延迟启动。
   - 结束基准 = 所有可映射流中最晚有效末样本的 Unix 时间；偏移为负表示该流提前结束。必须给出两个基准，不能把末帧相对起始基准的时长称为结束偏移。
   - 视频用 `start_offset_us + 实际 packet PTS` 计算首末帧，不能只用容器 duration 代替；Ego 视频使用自己的 metadata。Ego 单调时钟 topic 的首末时间使用同一个固定 offset，不逐流重新拟合。
   - 缺文件、无样本、映射缺失或无法检查的流仍列出，未知项写 `N/A` 并说明原因；音频没有可靠 Unix 锚点时只检查其包时间轴，不根据文件 mtime 或邻近视频猜测起止时间。
2. **所有模态 gap 简表**：流名、时间口径、样本 / 帧数、典型间隔 ms、最大间隔 ms、报警阈值 ms、超阈值次数、重复次数、回退次数；异常时补充时间点或少量最大 gap 样本。
   - 按文件原始记录顺序检查相邻时间差，先排序会掩盖回退。超阈值次数为 0 时仍展示最大间隔和阈值，不能写成“没有 gap”。
   - 双目 MKV PTS 可能压紧真实采集时间洞；同时列出 Fays camera 的设备时间与 Unix 映射时间统计，不能以视频包连续证明双目没有丢帧。Fays IMU 也分别列这两种口径。
   - 编码器角度不补造不等于时间戳未处理。确认对应版本是否存在批次时间戳平滑 / 单调钳制，注明“主机接收 / 平滑后时间”；这类 gap 只能说明落盘时间轴间隔，不能直接断定硬件断采或无漏样。有原始时间 / 序号时再交叉判断。
   - 跳过扫描、解析失败或数据不足时标明 `未检查 / N/A`，不得填零或宣称已通过。全模态覆盖有缺口时在结论中明确列出。

文本入口默认打印这两张 Markdown 表格；JSON 的 `summary.modality_timing` 保留两个基准、逐流起止偏移与 gap 统计。原有 `global_first_frame_sync`、findings 和详细统计仍保留。

## 建议重点补充的异常类型

除用户已提出的项目外，额外重点关注：

- 文件存在但样本数极少，说明链路可能起了但几乎没录到。
- 单路视频能播但时间轴回退，说明 mux 或时间戳可能异常。
- 双目 `stereo_session` 与实际文件首尾不一致，说明 warmup/session 边界可能失配。
- 左右同类 sensor 都正常，但视频与 sensor 整体错位，说明跨进程起录边界可能异常。
- 左右 sensor topic 内部对齐正常，但整体相对视频偏移很大，说明更像跨链路时间基准不一致，而不是 sensor 自身起录抖动。
- 只有某一侧 encoder 尾部缺失，说明串口或写线程可能中途掉线。
- 触觉相机与主相机 / stereo 普遍错开，说明该类设备独立启动或 stop 边界异常。
- 所有流都存在相近时刻的大 gap，说明可能是系统调度 / 写盘抖动，而不是单设备掉流。
- `metadata` / `calibration` 不完整但媒体文件存在，说明这条数据可能“录到了，但不适合直接入库”。
- 主摄只有 decode 侧 non-monotonic dts 告警，但无缺参考帧或解码错误，说明更像时间戳封装问题，不一定是坏流。

## 输出要求

优先用简洁、可执行的结论，不要只堆原始统计：

1. 总结该条 episode 是否可用。
2. 列出最严重的异常，按严重度排序。
3. 每个异常都给出证据：
   - 文件或 topic
   - 时间点
   - gap / 偏差 / 时长差
   - 必须输出上述全模态起止偏移表和 gap 简表，并保留首帧 Unix 时间、换算方法与相对误差；单独说明启动覆盖差、中途时间洞与结束覆盖差
4. 若能判断模式，明确写出高概率归因：
   - 单路掉流
   - 多路同时卡顿
   - 首帧未对齐
   - sensor 早停
   - 视频与 sensor 起止窗口错位
5. 最后给出下一步建议：
   - 是否建议重录
   - 是否需要看运行日志
   - 是否需要继续排查 camera_recorder / sensor_recorder / record_runtime

## 资源使用

- metadata 3.3 在 data_version 后包含 13 个版本字符串（新增 rk3588_firmware_version 位于 ego_firmware_version 后、hardware_list 前，来自 /home/user/version.info，复用现有版本缓存；历史 3.3 缺少该新增字段仍兼容）；ego_bound（布尔值）紧邻 ego_software_version 之前，hardware_list 末项为 ego_sn（未绑定为空）。主摄固件版本取 USB iSerial；胸摄未启用、+nostereo 双目固件写 "disabled"；未绑定 Ego 两个版本为空。起录前所有启用设备版本必须已知，缺项拒绝创建 episode，待机继续限频重试；起录冻结版本和绑定 SN，停录只消费内存副本。其余 metadata 字段、顺序、类型及 calibration 格式保持不变。Ego 拉回并完成远端大小校验与清理后，在本地 ego/metadata.json 写 related_episode_name 和 related_episode_uuid，分别等于 UMI episode_name（不带 -temp）和 data_uuid。 范本：`.codex/skills/validate-episode-data/references/metadata-v3.3.json`。
- 阈值口径与检查项说明：读取 `references/checklist.md`
- 测试工程师默认入口：执行 `bash test/scripts/validate_episode_data.sh <episode_dir>`
- skill 内部实现：`scripts/validate_episode.py`
- 主摄专项时间戳排查默认会复用 `test/scripts/scan_main_camera_mkv_issues.py`
- 只在用户明确要求修复代码时，才切换到修改流程；本 skill 默认保持只读分析

Ego 关联字段示例（仅展示新增字段，原 Ego 身份及其他字段保持不变）：

```json
{
  "related_episode_name": "episode_20260727_0001",
  "related_episode_uuid": "test-uuid"
}
```

两值对应 metadata-v3.3.json 范本的 UMI episode_name/data_uuid；已绑定时主 metadata 的 ego_bound=true、hardware_list.ego_sn 为绑定 serial，Ego 软件/固件版本均为实读非空字符串。
