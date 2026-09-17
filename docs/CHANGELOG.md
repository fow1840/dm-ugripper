# Changelog

- 内置 BMS 电量读取、断连重试和状态快照，Web 自动刷新背包电量；Ego 联动低电量、延迟数据收尾/低存储、校验失败和触觉损坏播报，按阈值/会话去重。
- 双目停录等待在途编码/IMU 提交并排空 MCAP 后才确认关闭；起录等待 MCAP 打开，修复视频末帧缺少 MCAP 时间戳。
- 按 episode_20260917_0002 毫秒表启动后前 10 个有效帧更新逐路固定时间补偿；不调整共模漂移，未标定的左双目保留原值。

- RK3588 数采服务启动时将内核 unbound workqueue 限到现有允许的大核集合，缓解 UVC 拷贝/接收请求补充延迟导致的双目短帧；停止时恢复原掩码，保留外部配置修改，提供 systemd 环境开关关闭。

- 触觉相机转码保留 V4L2 时间基及真实帧间隔，禁止自动凑帧率补帧/丢帧；绝对起点改用输入采集 Unix 时间，移除原首个编码输出接收锚点的 −35ms 补偿，等待采集时钟口径重新标定。

- 双目采集与消费解耦：取帧线程移除日志/文件/标定/快照处理，采用可复用的有界帧队列；独立维护采集健康和耗时统计，并按采集会话隔离积压帧，暴露 SDK 时间缺口与队列溢出计数。
- 双目灰度录制改用 NV12 管道直送 RKMPP，减少像素转换和管道数据量；保持 H.264/H.265、有限范围 YUV420 输出，保留三通道 BGR 兼容。

- Web 配置页新增时间同步测试：可设置录制时长、组数、毫秒表延迟启动，后台自动逐组录制与清零；支持进度、取消和持久化结果，每组保存 stopwatch_timing.json。

- 录制标定格式升级为 3.1：空闲时读取并缓存左右夹爪 UCAL V2/V3 眼眼、手眼外参，按存储值直接写入主摄 extrinsics；缺失项使用默认结构参数，新增独立主摄外参标定状态。

- Web 语言选择器增加固定双语标签 `Language / 语言`，录制数据列表改为按名称倒序分页。


- Web 新增离线生产压测 A/B 共 62 轮、取消、失败统计与持久化结果；短测 ≥3、长测 ≥2 或总失败 >3 判不通过。
- Web 编码器改为保持串口连接的 100 Hz 目标采样流，显示实际频率，断连与 60 秒截止释放串口。
- Web 默认随设备语言显示中英文，支持右上角临时切换；删除拔网线重启数采服务行为。


- 修复网页配置确认后读取已失效的事件 currentTarget 导致保存请求未发出；配置表单在弹窗前保存引用。

- 每张触觉传感器图片旁增加实时黄/绿/灰告警标签，与本次检测结果分开展示；断连或传感器更换时提示未知/重新取图。

- 触觉正常/损坏例图改为单张图片与图外标题，损坏样本使用指定实拍 JPG，去除例图中的基线和 mask 拼图。

## Ego 版本与触觉盖板诊断（2026-09-15）

- 绑定 Ego 后展示固件、软件版本，在线定期刷新，断连标记缓存。
- 触觉区域按夹爪侧别和传感器位置显示，新增“盖板损坏检测”、真实参考例图与长短期基线 mask 状态图。
- 支持确认后重置指定传感器的长短期基线并同步告警；临时结果有界保留，结束及启动时清理中间文件。

## 手动相机抓图（2026-09-15）

- 新增 `getimg` 与 Web 本机相机抓图按钮，按采集参数直接抓图；双目从现有 recorder 获取下一帧，持续预热。
- 新增独立 Ego 抓图按钮，短录约 3 秒后拉取三路视频截帧，成功后删除本次远端测试数据；录制忙、取帧失败和清理失败显式报告。

- Web 设备管理改为以太网免登录访问，移除密码生成、登录和退出入口；覆盖安装清理旧密码配置，保留 HTTPS、跨站写请求防护、维护确认和录制互斥。

- 新增独立 HTTPS 设备管理页面：展示传感器与双目出流、资源和磁盘状态，支持日志获取、数据下载、白名单 deb 覆盖安装、左右主摄/夹爪固件批量升级和治具确认后的编码器标零。录制、数据传输与维护使用原子互斥锁；随包提供并强制使用主摄 DFU 工具。

## Unreleased

- 主摄 50 Hz 重复帧修正：H.265 结合局部编码间隔、相对 VCL 大小及 POC/V4L2 连续性，锁定后保留六帧周期辅助，异常重新捕获。仅修改重复帧及随后两帧，每帧独立使用 XU 基准映射；主摄绝对补偿为净 -28 ms。0008 有效 LED 标定范围左右各 980/980 命中、无额外预测；0012 的 100 秒 H.265 录制通过逐帧 PTS 映射及解码检查。H.264 周期校时已接入，但其重复帧与曝光补偿仍待独立适配验证。

- 修复双目服务重启后残留 FIFO 导致维护任务被 `set -e` 提前终止、无法重新拉流的问题；健康检查非零结果继续进入恢复流程，维护任务失败或结果为空时记录错误并保留原有状态。

- H.264 主摄接入与 H.265 相同的 XU SET/GET、10 秒周期校时和 XU2 SEI 逐帧 Unix 时间映射；统一过渡帧有符号差值、PTS 单调钳制和失败处理。H.264 尾置 SEI 在封装前移至本帧 VCL 前，避免错帧与尾部孤立包。新增 H.264 离线 SEI 解析命令及基准应用日志；下一轮校时前仍未观察到当前基准时中止该路录制。

- 修复主摄升级后因固件 iSerial 变化而误报单侧回归失败：硬件身份仅核对物理端口、SN 与端点包长，目标固件版本独立回读确认；保留逐侧失败详情与完整 DFU 事件日志。

- Web 配置页新增空闲编码器读数入口，持续显示左右原始计数、角度和弧度；使用录制/维护互斥锁，仅发送 Modbus 位置读取，停止或离开页面即结束轮询。

- 固件状态改由 UGripper 运行时自动维护，Web 仅读取左右主摄 iSerial 与夹爪版本，移除手动读取按钮；夹爪启动/重连后在空闲时读取并有限重试，录制中不发版本查询。

- Web 升级维护页的软件版本与固件信息并排展示，名称和版本左对齐紧凑排列，窄屏自动改为上下布局。

- HWS 与 Web 设备概览新增 Ego 绑定、连接状态和绑定序列号；共用运行时快照判定，快照过期或不匹配时显示未知，未绑定时不推断物理连接。

- Web 维护页按左右展示主摄 USB iSerial 和夹爪版本，支持空闲时读取夹爪版本；编码器标零改为左右独立操作。配置页新增语言、视频编码、胸部主摄开关、电源频率四个下拉项，保存告警后经 root 白名单 helper 原子更新并重启数采。

- 双目初始化等待与启动健康宽限期调整为 8 秒，避免左侧 SDK 约 4.3 秒的初始化被提前终止；出流后视频/IMU 断流阈值保持 3 秒，节点稳定等待 3 秒，停止释放后等待 1 秒重试。

- 停录时增量保存 systemd 系统日志到 U 盘 `logs/system_<设备SN>_<日期>.log`，通过游标和内容去重支持重启续写，采用原子替换避免中断损坏。

- 修复双目 SDK 正常帧间 `NO_NEW_FRAME` 被误报 WARN：连续无帧达到 200ms 才告警，SDK 异常按返回码独立限频；诊断改用单调时钟，轮询累计次数不再表述为丢帧数，并限制采集慢处理日志频率。

- 日志错误提示改为一句简短中文，直接说明故障位置和问题，不再展示英文类型及映射条件。

- 修复 H.265 主摄周期校时后的过渡帧被误判非法：允许新 SEI 基准下 `Pframe<Pset`，按有符号差值换算 Unix 时间并保留溢出检查，避免首次 10 秒校时后主摄短流。

- 日志 WebUI 按错误码对照表在日志下方标注原因参考，保留多条件映射与未知根因语义，离线使用无需访问仓库文档。

- 日志 WebUI 改为一次仅查看一份日志，新文件替换当前内容并重置筛选，统计和导出仅针对当前文件。

- 日志 WebUI 新增独立 error code 下拉筛选，展示各错误码次数并支持与其他条件组合。

- 新增 `tools/log_viewer/` 独立离线日志 WebUI，支持单文件导入、等级/时间/关键词筛选、restore 事件统计与时间线、上下文查看和筛选结果导出。

- Ego 绑定时读取 App 版本，`>=1.0.4` 按主机硬件版本配置输入电流：`2.5.x >2.5.0` 为 1.6A，其余回退 1.1A；核对请求值、回读值和持久化结果，配置失败仅告警。

- Ego 语音仅接入存储空间不足 `1203`，App 需 `>=1.0.4`，同一次 `disk_full` 只触发一次，语言跟随主机配置；不接入人工无效 `1103` 和数据写入 `1003`，播报失败仅告警。

- H.265 主摄录制中每 10 秒通过 XU 更新时间基准，按帧 SEI 匹配新旧基准并补偿 PTS；同步修正超过 10ms 或时间回退时输出 WARN，回退帧以最小单调钳制防止 PTS 倒退。

- 触觉损坏判断由连通域面积门限改为每连通域至少 `8` 个 `3x3` 实心核心像素，整体过滤无法形成核心的 `1~2 px` 细直线、曲线和贴边轮廓；默认关闭并短路全局平移配准与边缘局部容差，新增核心像素和 curve 过滤诊断，降低误报与单帧计算开销。
- 新增可独立交付的单帧触觉损坏检测工具：完整实现收口到 `tools/tactile_damage_detector/`，输入基线图与待测图，输出 JSON 结论、最终实心核心 mask 和一张对比图，且不调用仓库其他脚本；交付包不携带生产或测试图片。
- 触觉亮度校正增加严格条件启用的九分位单调 LUT，用于过滤同一纹理的非线性曝光变化；同时增加全局与分块结构相关性门禁，换盖板时提示刷新基线但不自动覆盖，大面积局部结构损坏仍进入损坏判定。
- 外部秒表联动脚本不再携带或安装 CH9344 helper，也不修改 udev 状态；预检不硬编码左右手物理 USB bus，只要求板端正式 UGripper 生成的四个 CH9344 逻辑链接各自具有唯一声明者且 symlink 指向一致。
- 双目 Fays recorder 增加每个 episode 的标定 JSON 最多 5 次重试，并以限频 `WARN` 记录 SDK 无新帧/超时/设备丢失、采集处理过慢和编码队列丢帧。

## v2.1.12 - 2026-09-07

- 触觉损坏软校验新增低开销空间容错：先执行 `±2 px` 全局整数平移配准，再仅对双侧纹理边缘开放双向 `1 px` 局部容差，过滤长边线的 `1~2 px` 漂移误报；保留亮度/对比度配准、MAD 阈值、邻域投票和连通域过滤，并在诊断日志中记录配准位移、评分及边缘抑制像素数。
- H.265 主摄改用 UVC XU RTC 与码流 XU2 SEI 建立 Unix 时间轴：起流前写入主机 Unix 时间 `Tset` 加 `200us`，GET 读取固件在 SET 时锁存的 encoder `Pset`，每帧 Unix 时间按 `Tset + Pframe - Pset` 计算；首个实际写入的 clean keyframe 以其换算后的 Unix 时间写入 metadata `start_offset_us`，后续 PTS 保留 encoder 实际间隔。主摄原 `36ms` 后处理补偿暂置为 `0`，H.264 继续使用 V4L2 时间；tcam 改为向前校正 `-36ms`，stereo camera `publishTime` 改为向前校正 `-3ms`。
- 主摄新增通用命名的 `UGRIPPER_POWER_LINE_FREQUENCY` 环境配置，安装时默认写入 `50` Hz，可改为 `60` Hz 以匹配供电频率；当前仅主摄启动时按配置下发 V4L2 防频闪参数。
- metadata schema 更新到 `data_version=3.2`：在 `task_stop_s` 后依次新增 Unix 秒开机时间 `boot_time_s` 和触觉损坏 SN 列表 `tactile_damage_sn_list`；后台触觉校验按本条 episode 单次 `damaged=true` 原子回写对应 SN，无损坏保持空列表。停录收尾调整为先同步 `video stop` 运行日志，再释放录制锁；离线 episode 校验器同步支持 3.2，并兼容历史 3.1/3.0。
- UgripperRuntime 完成领域拆分与接口收口：将录制、设备、校验、恢复、服务和持久化职责从单体实现拆到独立模块，补齐 ownership/dependency ports，统一 `DA::Ugripper` 命名与公开 API，并对齐 pp-main 公共日志/工具库及 host-only 单测边界；外部录制行为和安装布局保持不变。
- 外部秒表录制联动脚本升级为 schema v2：常驻 root Python helper 预打开 GPIO fd，并在同一进程内用 `CLOCK_REALTIME/CLOCK_MONOTONIC_RAW` 夹逼 `write()`，替代正式触发路径中的 `date + sudo tee` 多进程调用；按“低电平持续运行、高电平保持至少 5 秒清零”的硬件合同，在录制前和停录后各执行一次高电平清零保持，异常退出时也完成清零；只校验板端现有 CH9344 映射，不携带或安装串口 helper，也不修改 udev 状态；`stopwatch_timing.json` 增加 GPIO start/stop 写入边界、前后高电平保持时间、清零完成时间和微秒级窗口。
- 新增快速 ARM deb 部署入口 `scripts/deploy_arm_deb_fast.sh`：根据 source manifest 自动选择 full/quick/skip 构建模式，增量传输指定主包和可选 updater，并在目标板安装后回读包版本及 `ugripper.service` 状态。
- 构建脚本默认主包版本调整到 `2.1.12`。

## v2.1.11 - 2026-09-02

- 主运行日志统一为 V3 单行合同，C++、Python、Shell 与外部 child 使用同一 Unix 微秒/native TID/等级/调用点外层；既有错误分支追加显式 `UGR-xx-xx` 错误码，并补齐日志合同检查工具。
- Ego 起录前时间同步增加严格 `<15ms` 误差门限，每次绝对授时固定补偿实测 ADB 生效延迟 `20ms`，未达标时最多重试三次；连续失败以 `UGR-82-02` 中止本条录制并进入 `ERROR_5`。
- 修复 Ego 解绑后状态仍残留 `connected` 及旧 serial 目录遮蔽实际 episode 的问题；绑定 serial 精确路径、新旧 serial-scoped 路径、legacy root 与 fallback 扫描结果统一汇总去重，并阻止解绑前的旧 probe 回写状态。
- Ego 停录改为最多 `3` 次 STOP 广播并固定 `100ms` 重试间隔；录制中设备消失统一上报 `ego_disconnected/ERROR_5`，finalize 文件大小不一致时按 offset 有限重拉，恢复未完成时保留远端结果。
- Fays VIKit SDK 从 `3.8.0` 更新到上游 `v3.8.3`（`8e2d91a08b065bde247a2283ecaa4bb7fd3aa419`），修复 IMU 参数读取和帧时间戳小概率回退问题，并优化数据流初始化稳定性；同步 aarch64/x86_64 运行库与版本头，FT602 `1.0.17` 保持不变。
- 相机全局时间补偿配置集中到 recorder 各自的头文件，V2.1.11 发布值保持为 `0`：主摄、tcam 与 stereo camera 均不额外调整全局 Unix 时间，视频 PTS、Fays 原始 `logTime` 与 IMU 时间保持不变。
- metadata 发布格式保持 `data_version=3.1`，不生成 `boot_time_s` 和 `tactile_damage_sn_list`；后台触觉校验继续记录历史，离线 episode 校验器兼容已有 3.2 数据及历史 3.0。
- GripperHmiTool 新增 HMI 双版本字段只读查询，分别输出 `version_56` / `version_57`；新增显式高风险初始化命令，在初始化前缓存并在初始化后逐字节恢复、回读验证 SN 与标定 payload，失败时最多执行 3 轮完整恢复。
- 停录写盘后返回 READY 时，左右绿色呼吸灯从约 `70%` 最大亮度的上升段同步开始；持续 READY 与夹爪重连不重复重置相位。Ego 绑定、解绑及绑定失败提示音结束后按侧回读确认蜂鸣器为 `0/0`，未关闭时最多重试 `5` 次。
- 收紧 CH9344 外部 hub 映射，只允许 bus1/bus9 分别映射右/左夹爪并排除 bus5/bus11；新增 PC 端外部秒表录制联动脚本，通过 runtime FIFO 启停录制、使用 GPIO139 触发秒表，并在 episode 写入 `stopwatch_timing.json`。
- 构建脚本默认主包版本调整到 `2.1.11`。

## v2.1.10 - 2026-07-29

- 主相机 MKV 的 PTS 改为使用 V4L2 buffer 实际时间，保留丢帧和采集卡顿产生的真实时间洞；stereo 逐帧同步继续以 Fays MCAP camera `publishTime` 为准。
- 修复 HMI 端口长期无响应时故障证据持续变化、健康错误重复上报及 USB restore 稳定窗口反复重置的问题。
- 离线 episode 校验统一检查全部视频、encoder 与 Fays IMU/camera 首帧的全局时间对齐，并新增版本间测试重点汇总工具。
- 构建脚本默认主包版本调整到 `2.1.10`。

## v2.1.9 - 2026-07-28

- 新增 SXR Ego 显式绑定和录制任务起止标记；绑定改为开机周期状态，metadata 更新到 `data_version=3.1`，并修复准备阶段被误判为离线的问题。
- 修正 stereo 元数据时间基准：offset/duration 改用 Fays MCAP camera 首尾时间；Episode 深度校验器支持按 `+nostereo` 版本选择产物清单并兼容历史 schema。
- 修复 HMI 蜂鸣偶发持续响及周期灯效错相问题；扩展四口手部 USB2 hub 映射，并修复 auto-release manifest 扫描卡顿。
- 构建脚本默认主包版本调整到 `2.1.9`。

## v2.1.8 - 2026-07-24

- Fays VIKit SDK 从 `3.5.1` 更新到上游 `3.8.0`，同步 aarch64/x86_64 运行库、公开 API 与版本头；FT602 `1.0.17` 保持不变。
- 构建脚本默认主包版本调整到 `2.1.8`。

## v2.1.7 - 2026-07-20

- 新增 `+nostereo` 构建变体：关闭 Fays daemon、stereo 健康检查、双目 session/finalize 及相关产物生成和校验；默认包仍启用 stereo。
- Ego dataset 目录发现支持按设备 serial 扫描，兼容新旧 SXR 数据路径。
- 构建脚本默认主包版本调整到 `2.1.7`。

## v2.1.6 - 2026-07-14

- HMI 物理按键增加按下、松开双向稳定滤波，过滤短暂电平毛刺并保留既有短按动作间隔。
- UgripperRuntime、SensorRecorder 与 encoder zeroing 的信号处理改为仅设置停止标志；SensorRecorder 退出时先等待读写线程停止再关闭串口，避免服务停止或停录时死锁、资源竞态及 MCAP 未封口。
- 构建脚本默认主包版本调整到 `2.1.6`。

> 说明：本文件只保留 v2.0.0 以来的高信号发布变更；当前系统行为以 `docs/agent/overview.md` 为准。
>
> 仓库没有对应的 v2.0.x/v2.1.x git tag。下面的发布边界按 `build_deb.sh` / `scripts/build_arm_deb_in_pp_arm_dev.sh` 中 `BASE_VERSION` 的提交记录推定。

## v2.1.5 - 2026-07-13

- 物理 `BTN_UP` / `BTN_DOWN` 录制启停改为 `1s` 内同键双击触发，软件 FIFO 控制命令仍保持单次触发；物理按键释放防抖由 `250ms` 调整为 `80ms`，避免第二次短按被过滤。
- 新增停录后人工失败补标：空闲时左手单键长按可将上一条完成 episode 的 `metadata.json` 标记为 `operator_marked_failed`，同步写入 `validation_error.log`；成功后紫灯闪烁与双手蜂鸣器两次提示同步。ACI 多文件 episode 保持四个独立 sensor/Fays MCAP，不向数据 MCAP 注入 metadata topic。
- 触觉状态比较统一将画面左侧裁剪比例由 `12%` 调整为 `18%`，使运行时抽帧与离线残差检查使用相同有效区域。
- 新增 encoder 命令 ACK 延迟诊断，以及触觉相机 FFmpeg 单路反复 STREAMON、四路并行启停压力测试脚本。
- 增强 240 现场安全 soak 与掉线复现流程，补充自动录制、日志抓取和 Foxglove episode 打开辅助入口。
- 新增 HMI 链路压力、CH9344 分组干扰和 encoder 到 HMI 反向干扰诊断工具，用于区分单 UART、夹爪控制板、CH9344、Hub 与 USB 上行链路故障。
- 构建脚本默认主包版本调整到 `2.1.5`，并已完成 ARM64 打包与 240 设备安装验证。

## v2.1.4 - Unreleased

- 主摄压缩码流在写入最终容器前等待包含参数集与 IDR 的 clean keyframe，避免设备冷启动坏首包进入 MKV；停录时若 EOS 已被轮询线程消费，则不再重复等待并误报 `bus_wait_timeout`。
- HMI 蜂鸣关闭改为发送命令后回读确认，状态不为 `0/0` 时最多重试 `5` 次。
- CameraRecorder、Fays recorder 及运行时直接启动的辅助子进程在 `exec` 前关闭非标准 fd；Fays daemon 启动左右 recorder 时不再向下继承顶层控制 FIFO，避免 ffmpeg 或 recorder 额外持有 CH9344、相机、IMU 等无关硬件资源。
- 构建脚本默认主包版本调整到 `2.1.4`。

## v2.1.3 - Unreleased

- 子进程启动前主动关闭未白名单继承的 fd，并为主摄 XU 与夹爪串口 fd 设置 close-on-exec，避免 Fays recorder 继承主摄、串口等父进程外设句柄。
- 新 hub 评估板 udev rules 保留板端已验证映射：右侧主摄 `1-1.4`、左侧主摄 `9-1.4`，四路触觉分别覆盖 `1-1.3.1/1-1.3.6/9-1.3.1/9-1.3.6`。
- 数据盘未挂载或不可写时只保持等待/`ERROR_3`，不进入 USB restore 复位流程。
- 构建脚本默认主包版本调整到 `2.1.3`。

## v2.1.2 - Unreleased

- 自动复位 symlink 齐全后的 ready 等待窗口从约 `25s` 提到约 `45s`，降低 Fays stereo daemon 重启和主摄缓存刷新接近边界时被误判为复位失败的概率。
- Fays stereo daemon 不再按单侧 SDK startup complete 串行启动左右 recorder；同轮维护中会直接拉起可用侧 recorder，ready 状态继续由健康检查收敛。
- 录制中新增 CameraRecorder 主摄 V4L2/USB 故障直达 `ERROR_2`：主摄启动阶段若返回 `VIDIOC_*`、I/O error、设备消失或超时，CameraRecorder 会写入 `/dev/shm` 故障证据，`record_runtime` 立即中断录制并触发现有 USB 复位流程；主摄录制窗口达到约 `5s` 后若仍只有明显短流，也按同一路径处理，`5s` 内快速启停不触发短流判定；CameraRecorder 内核 D 状态兜底阈值同步收敛为 `5s`，用于应用层无法返回错误的 ioctl 卡死场景。
- 构建脚本默认主包版本调整到 `2.1.2`。

## v2.1.1 - Unreleased

- 新增错误态夹爪传感器复位入口：`record_runtime` 会在 `ERROR_2`、`ERROR_4` 以及主摄相关 `ERROR_1` 时异步触发 `/usr/local/sbin/ugripper_restore_usb`，`ERROR_3` 磁盘类错误和 `ERROR_5` 运行时兜底错误不触发；触发复位前会先暂停 Fays stereo daemon/recorder，避免 SDK 在 USB 断电重枚举窗口占用双目节点；每次复位完成后会按约 `1s` 周期检查全部关键传感器 symlink，symlink 齐后立即重启 Fays daemon 并开始健康恢复计时，最多等约 `25s` 仍缺 symlink 才重试；symlink 齐后最多继续等约 `25s` 检查健康状态与主摄缓存，仍未恢复则重试；等待窗口内如果健康状态提前恢复，会立即认定本轮复位成功并清除等待窗口，后续再异常按新的故障窗口处理；同一错误窗口连续 `3` 次失败后通过夹爪 HMI 蜂鸣器报警，单次蜂鸣最长 `5s` 后自动关闭。sudoers 允许 `ubuntu` 用户免密执行固定 root wrapper；wrapper 会在存在 `bluetooth_gatt.service` 时先停止以释放通信接口，不存在或停止失败不阻断复位主流程。
- 自动复位断电前新增数据盘保护：录制中先按错误停录完成 episode 收尾，随后同步运行日志并通过 root system action 卸载 `/mnt/data_disk`；数据盘卸载失败时跳过本次复位且不消耗复位次数，避免 U 盘读写中被 USB 供电复位硬断。
- 自动复位新增人工插拔保护：某侧夹爪完全未枚举时只保留错误提示，不触发软件复位且不消耗复位次数；检测到该侧任一关键节点或 HMI 重新出现时会重置复位次数，并给该侧 `20s` 插入稳定窗口，窗口内健康检查错误不触发软件复位。
- 自动复位触发日志收敛为 `restore usb requested cause=... evidence=... side=...`；symlink/硬件健康缺失类触发会先进入约 `6s` 自恢复缓冲窗口，窗口内同一证据持续存在才执行复位，避免 symlink/udev 短暂抖动或可自恢复重枚举直接触发 USB 断电；主摄校验类一次性证据仍会直接触发。录制中硬件故障停录后，即使本条 episode 质量校验失败，只要录制已经停住，也会进入该缓冲窗口等待确认。恢复等待阶段只在缺失列表变化或约 `5s` 间隔输出 symlink 缺失证据。
- `run_record.sh` 默认导出 `mpp_debug=0`、`mpp_log_level=2`、`mpp_syslog_perror=0`，压低 Rockchip MPP 编码库的 `mpp_info`/`mpp_enc` 正常配置噪声，避免录制时 journal 被编码器初始化信息刷屏。
- 删除 Fays recorder 控制 FIFO 启动时的 `[Control] Entering command loop` 与 `Supported commands` 提示日志；保留 START/STOP、FIFO 读写失败和未知命令等有效事件。
- 自动复位跳过人工插拔稳定窗口或整侧未枚举时不再按轮询周期重复输出 `skip restore usb ...`；保留人工插入、拔出、稳定窗口结束、复位请求、symlink 缺失/超时与最终恢复/失败日志，便于直接看到复位原因及证据。
- 自动复位在等待本轮复位 settle、已达到最大尝试次数、preflight cooldown 或复位命令仍在运行时不再按轮询周期重复输出 `skip restore usb ...`；`restore command` 缺失或不存在仅在原因变化时输出一次。
- 夹爪 HMI 串口驱动不再按 `30s` 周期输出 `[GRIPPER_DIAG] category=io_summary reason=periodic`；保留断连、IO 失败和独占命令失败摘要，降低正常待机日志噪声。
- 删除 Fays stereo daemon 健康维护循环中的 `stereo recorder start order` 调试日志；保留 recorder 启停、SDK startup、unhealthy/restarting 等有效事件日志。
- 修正夹爪供电复位写寄存器方式：电源板 `0x10` 寄存器恢复按低 4 bit 逐位写入，上电阶段保留短间隔错峰上电；每个 bit 写入后读回校验，整组完成后再次校验，失败时有限重试，避免偶发读写失败直接终止复位。
- 收口系统动作触发单元归属：`umi-shutdown-trigger.path/service` 改为由 `ugripper` 主包唯一交付并在安装后显式启用/启动，`ugripper.service` 启动时也会补拉 path listener；`das-usb-updater` 不再打包同名 unit，避免 `/lib` 与 `/etc` 下的同名单元互相覆盖导致旧版系统动作请求文件无人消费。
- 系统动作文件触发通道从 `/tmp/umi_system_action_*` 迁移到 `/run/ugripper/system_action_*`，并通过 tmpfiles.d 在开机阶段创建 `/run/ugripper`，规避系统镜像中 `/tmp` tmpfs 与 swapfile 链路的 systemd boot transaction ordering cycle 导致 path unit 激活遗漏。
- Python 运行环境新增 `pymodbus`，并将默认打包 `.venv` 切到 `py311_v2.1.0/ugripper_venv_20260623_102954_arm64.tar.gz`；`pyserial` 继续锁定在现有依赖中。
- 构建脚本默认主包版本调整到 `2.1.1`。

## v2.0.13 - Unreleased

- 主摄 V4L2 MMAP 缓冲池改为 `v4l2_buffer_count` 可配置，并将默认主摄缓冲数从 24 收敛为 16，降低 RK3588 现场 `alloc_contig_range ... PFNs busy` / CMA 压力导致主摄 UVC 起流失败的概率，同时避免回退到历史 8-buffer 口径带来的短时背压/坏流风险。
- Fays stereo raw frame 队列收敛：`VideoFrameQueue` 默认容量从 128 降为 32，ffmpeg rawvideo `thread_queue_size` 从 512 降为 64，降低双路 stereo 常驻/录制切换期间的大帧内存占用。
- 触觉相机 SN 改为运行时缓存：在 `/dev/tcam_*` 插入或 symlink 目标变化时通过 USB sysfs serial 刷新，metadata、calibration 与后台 tactile 校验只消费缓存，减少停录阶段外部探测操作。
- 触觉传感器持久化 baseline 存储目录由 `/tmp/umi_tactile_state` 改为 `/var/lib/ugripper/tactile_state`，避免重启后 persistent baseline 被清除导致无法检测关机期间发生的盖板损伤。
- 触觉软告警黄灯改为按侧别与传感器位置编码：左侧分别使用一长一短/一长两短，双路异常时使用两长，左右两侧可同时显示各自编码。
- 升级/重装时在 `prerm` 阶段自动清除触觉传感器基线目录，避免旧版本基线持续污染新版本。
- 录制起停的 stereo 控制改为 `record_runtime` 直接写左右 Fays recorder FIFO，停录时并发发送左右 `STOP`，并统一 C++ FIFO 完整行读写工具，避免顶层 shell FIFO 超时读半行导致 `STOP` 被截断。
- 双目 stereo 的时长与 span gap 校验改用对应 Fays MCAP camera 帧首尾时间跨度，MKV 仅保留存在性与可读性检查，避免轻微丢帧造成容器时长偏短时误判数据失败。
- 触觉实时参考帧与持久化 baseline 改为夹爪重连后只标记待更新，由后续首个可用 episode 的触觉视频截帧生成，避免服务初始化或插爪阶段直接打开触觉相机。
- 单侧 Fays stereo 控制链路失效改为 `stereo_control_failed` / `ERROR_4` 侧别提示，识别不到 Fays 相机仍保持 `ERROR_2`；FIFO 启动超时日志补充 side、pid、设备路径和 runtime status 证据。
- 主包 postinst 安装窗口改为先停止并等待 `ugripper.service` 完全停稳，再重放 udev trigger，最后手动 start 并确认 active，降低 Fays warmup daemon 持有设备时触发 udev 导致 stereo USB 掉线的风险。
- Fays stereo daemon、wrapper 与 recorder 补充 `[FAYS_TS <HH:MM:SS.usec>]` 事件日志，覆盖 recorder 启停、健康重启、video port 占用清理、session start/stop/finalize 错误和 recorder 进程异常退出，便于和 `dmesg -T` USB 断连时间线直接对齐。
- Fays stereo daemon 改为按 stereo `/dev/videoN` 枚举顺序串行启动左右 SDK recorder，单侧等待启动完成或 `10s` 超时后再拉起另一侧，避免双侧 SDK 并行初始化互相干扰。
- 新增 `py_script/read_ugripper_mcap.txt` 通用 MCAP 读取示例及配套 README，供数据使用者直接解析当前 UGripper episode 中的 sensor、Fays 与可选 ego MCAP；使用 `.txt` 后缀便于发送。
- `[PERF]` 日志改为由编译包决定，默认发布包关闭；运行时不再读取 `UGRIPPER_PERF_LOG`，需要开启时由构建定义 `UGRIPPER_ENABLE_PERF_LOG=1`；Fays 错误、重启和必要状态日志不受该开关影响。
- ego 起录前按 `CAMERA_CODEC` 同步视频编码：`h264` 广播为 `avc`，`h265` 广播为 `hevc`，保持 ego 与背包编码格式一致。
- 构建脚本默认主包版本调整到 `2.0.13`。

## v2.0.4 - Unreleased

- 收口 SXR ego 联动采集：起录前同步 ego 时间，等待本次 temp episode 后再启动本机录制，并将 ego 数据直接落到当前 episode 的 `ego/`。
- 增强夹爪掉线复现 SOP：支持主机侧一键 SSH 启动连续软件录制测试、本地刷新 `hws` 状态，并在错误后自动抓取现场日志后暂停等待恢复。
- 加固 ego 停录收尾：修复 MP4/M4A `moov` 与文件变小残留尾部问题，显式 flush 后再安全清理远端 finalized episode。
- 新增本机软件录制控制 FIFO `/tmp/umi_record_control.pipe`，支持 `START/STOP/SHORT_UP/SHORT_DOWN` 并复用按键停录路径。
- Fays stereo cached calibration 复用增加 SN 校验，避免热插拔或单侧重启后视频与旧标定错配。
- 修正左手新 hub 触觉 `udev` 映射，并保留旧特殊 hub 兼容规则。
- 收敛 recording timing 诊断口径，补充 shm 路径、缺失 offset 和 flush/close 失败日志。
- 错误分类改为显式 `error_type`：支持同条 episode 内多个错误并存，`metadata.json` 结构保持不变且只写主错误类型，灯效按优先级播报一个错误；移除基于错误文本或 `/mnt/data_disk` 路径的猜测分类。
- 数据盘故障通过显式 fault key 映射到 `ERROR_3`，并收敛重复错误日志。
- `das-usb-updater` 的 deb 安装窗口恢复 HMI 灯效提示：安装中黄灯快闪，安装失败红灯，安装完成绿灯。
- 构建脚本默认版本调整到 `2.0.4`。

## v2.0.1 - 2026-05-21

- Episode 产物命名统一切到 2.0.1 口径：视频、传感器和 Fays 文件改为 `cam_left/cam_right/cam_chest`、`stereo_left/stereo_right`、`tcam_*`、`sensor_left/sensor_right`、`fays_data_left/fays_data_right` 等新命名。
- Episode 完成状态改为目录名表达：录制中使用 `episode_YYYYMMDD_NNNN-temp`，收尾完成后 rename 为最终 episode；最终产物不再包含 `info.json`，时间偏移迁移到 `metadata.json.video_details[].start_offset_us`。
- `metadata.json` 切到新 3.0 协议，增加硬件列表、必需文件、视频详情、采集时长和质量检查结果；`calibration.json` 改为按 schema 3.0 从零组装，避免旧标定字段混入。
- 主摄 SN 与标定改为设备插入/目标变化时异步维护运行时缓存，停录只消费缓存；缺少合法 SN 或标定会按 `calibration_error` 失败。
- Fays stereo daemon readiness、热插拔恢复、运行态 watchdog 和 FIFO 控制继续加固；录制中 stereo/finalize 明确失败时可快速结束并写失败 metadata。
- 停录和校验性能优化：相机与 sensor 并发 stop，视频探测和 Fays/encoder tail check 并行执行，补齐 Fays MCAP 与错误日志 flush。
- 硬件缺失类 `ERROR_2` 灯效改为侧别化提示；HMI SN payload 无效时直接拒绝，避免错误序列号进入 metadata。
- 新增 recording-safe NTP 同步链路：安装 `ugripper-ntp-sync.service`，只在非录制态校时，并通过 `/tmp/umi_recording.lock` 避免录制期间系统时间跳变。
- USB updater 迁移为独立 `das-usb-updater` 包，并保留过渡包构建入口，支持现场从旧 updater 迁移。
- 移除板载 IM648 采集链路：`sensor_recorder` 只保留左右 encoder，episode 校验不再要求板载 IMU topic。

## v2.0.0 - 2026-04-29

- 引入 Fays stereo 录制能力：新增 `standalone/FaysStereoRecorder`、Fays SDK 依赖和左右 stereo daemon，支持双侧 stereo 视频与 Fays MCAP 数据进入 episode。
- 录制 runtime 接入 Fays 预热、录制、停录与校验流程，补齐 stereo session/finalize 失败处理和 Fays tail/IMU 轻量检查。
- 相机配置迁移到 `standalone/CameraRecorder/config/camera_recorder.yaml`，Fays stereo 作为独立录制链路与主摄/触觉链路协同工作。
- ARM 构建与出包链路补齐 Fays recorder 目标、SDK 运行库和 `pp-arm-dev` 容器内一键出包脚本；默认主包版本提升到 `2.0.0`。
- 固定 USB 映射、camera recorder 配置和部署脚本同步适配 v2 Fays stereo 硬件拓扑。

- 新固件 `2026-09-16-2127` 的 H.264 接入与 H.265 共用的重复帧局部特征、六帧周期辅助及三帧 PTS 修正。新增渐进式 reference I/P 的 SPS/PPS/frame_num 解析，结合 V4L2 sequence 检测连续性；不支持的码流、错误帧和跳号解除锁定。保持每 10 秒 XU 校时、每帧独立锚点映射与净 -28 ms 补偿；补偿的实际光学误差仍需新固件标定。

- 主摄重复帧最小修复：已锁定周期且序号连续时，I 帧后候选以完整长短间隔确认，不再被压缩大小硬性否决；支持短间隔位于第二后续帧及相邻一帧重定相位。0014 独立 LED 有效范围左右各 982/982 命中、无额外预测，0008 H.265 预测不变；仅修改重复帧和随后两帧的既有映射保持。
