# Ugripper V2 Overview

本文档是当前仓库在交付、开发、排障和验收时的**唯一主说明**。
它只描述当前仍成立的系统实现、运行方式、部署入口和运维口径；不再拆分独立的运维文档、差异文档或遗留清单文档。

## 1. 当前系统一句话
- 当前系统默认以**单机双手、本地控制**方式运行。
- 主控制链路当前以 `run_record.sh -> /opt/ugripper/bin/UgripperRuntime/UgripperRuntime` 作为安装入口。
- 默认主包录制 **2 路主相机 + 2 路 stereo + 4 路触觉相机 + 左右 sensor/Fays MCAP**；构建时显式设置 `UGRIPPER_ENABLE_STEREO=OFF` 才生成不含双目数据的 `+nostereo` 变体。
- Fays 双目灰度图在编码线程直接转换为 NV12（Y 映射到 16–235，UV 固定 128），以 `-pix_fmt nv12 -color_range tv` 送入 FFmpeg RKMPP，支持 H.264/H.265；省去灰度扩展 BGR，保持 YUV420 有限范围输出。SDK 三通道输入保留 BGR 兼容路径；异常帧尺寸及会话中途格式变化拒绝送入固定布局管道。
- Fays 双目取帧线程只做 SDK 取帧、采集时刻对齐、必要的自有缓冲区复制、轻量计数和入队；64 帧有界环形队列以交换方式复用缓冲区（当前 640×800 灰度约 32 MiB/路，可容纳约 2.56 秒积压），锁内不分配图像、不编码、不打印。消费线程处理 NV12/编码、快照及标定；独立诊断线程写状态和限频告警，编码阻塞时仍能更新采集健康状态。队列满时丢最旧帧并累计 `video_queue_drops`，不阻塞 SDK 取帧；不能以线程分离承诺 SDK/调度/长期消费阻塞下绝对无丢帧。
- 帧携带采集时会话标识，待机及旧会话积压不能进入新 episode；快照只接受请求后的采集帧。`Time gap detected` 使用采集侧相邻 SDK 成功帧原始时间戳，超过 60ms 报告，在消费线程输出；应用队列溢出另计。runtime JSON 提供进程生命周期内 `capture_call_interval_max_ns`（相邻 SDK 调用起点间隔）、`capture_sdk_max_ns`、`capture_handoff_max_ns`（复制/入队耗时）、`capture_call_over_40ms_count`、`sdk_frame_gap_count`、`video_queue_high_water`、`video_queue_drops` 和 `invalid_video_frames`，用于区分取帧调度、SDK 调用和消费积压。
- Fays stereo recorder 每个 episode 的标定 JSON 获取最多尝试 5 次。SDK `NO_NEW_FRAME` 仅在连续无成功帧达到 200ms 时告警，首次出帧前从诊断线程启动计时；诊断线程每约 20ms 观察返回计数，每个返回码按 2 秒限频，`TIMEOUT`、`DEVICE_LOST` 等异常首次观测即告警。`return_count` 是分类累计轮询返回次数而非丢帧数；`no_frame_ms` 使用采集线程提供的单调时钟成功帧时间；队列溢出及无效尺寸按 2 秒限频告警。
- RK3588 数采服务启动前由 root helper `config/uvc_workqueue_affinity.py` 按在线 CPU 的 `cpu_capacity` 选择大核，将内核全局 unbound workqueue 掩码限制到现有允许集合中的大核，降低 UVC 异步拷贝与 USB 接收请求重新提交的调度延迟。该内核未暴露 UVC 专用 affinity 接口，因此此设置也影响其他 unbound workqueue，不改变应用线程优先级、SDK 取帧接口或坏帧丢弃规则。原掩码保存在 root 专用 `/run/ugripper-workqueue-affinity/`；服务停止、启动失败或升级停服时自动恢复，检测到外部修改则保留外部值。非 RK3588、没有大小核或现有掩码未允许大核时不调整，helper 失败记录 journal 且不阻止服务启动。可通过 `ugripper.service` 的 systemd drop-in 设置 `Environment=UGRIPPER_UVC_WORKQUEUE_BIG_CORES=false` 后重启关闭；不要用全局掩码调整代替逐帧校验。
- HMI 按键、RGB 灯效、提示音、pre/post 音频录制、停录校验，以及右手双键关机/左手双键卸载数据盘请求都已纳入当前运行时。
- U 盘流程统一负责 `deb` 升级/重装、`config.txt` 导入、标定数据导入和 encoder 零位校准触发；当前自动安装名单已覆盖 `das-usb-updater`、`ugripper`、`bluetooth-gatt-server`、`databot-device-joint` 与 `device-ota-mender`，deb 安装窗口会复用 HMI 灯效提示安装中/失败/完成状态，并会在整批安装完成后通过 PulseAudio 播放 `upgrade_completed.wav`。

Fays 双目恢复默认参数：启动 FIFO 等待、SDK 启动完成等待与启动健康宽限期均为 8 秒（240 实测左侧 SDK 初始化约 4.3 秒，3 秒会提前终止有效初始化）；IMU 无数据超过 3 秒、视频断流超过 3 秒视为不健康。停止并释放端口后等待 1 秒再尝试启动；启动前要求设备节点存在且指向连续稳定 3 秒（当前每次启动都会检查，因此该等待与停止后的 1 秒分别计时）。超时由约 1 秒一次的巡检判定，清理耗时另计。HMI 重连不作为人工插拔证据；软件 USB 恢复与健康验证期间及其后 20 秒内同步设备存在状态，不因重枚举清零恢复次数。

## 2. 安装布局与入口
- 安装目录：`/opt/ugripper`
- 主服务：`pack_script/ugripper.service`
- 主入口：`/opt/ugripper/run_record.sh`
- 主运行时：`/opt/ugripper/bin/UgripperRuntime/UgripperRuntime`
- 独立维护 Web 服务：`standalone/UgripperWebServer/` 提供 HTTPS 客户页面，接网线即可免登录访问（无账号、密码或会话），维护写请求保留跨站防护与操作确认，默认绑定以太网 `192.168.2.240:8080`，独立 `ugripper-webserver.service`（ubuntu）与 `ugripper-maintenance.service`（本地 root worker）。设备状态、日志下载、数据浏览/下载、白名单软件升级、左右主摄/夹爪固件升级、治具确认后的左右独立编码器标零通过受控 API 提供。主摄 iSerial 与夹爪固件版本按左右展示，由 UGripper 维护 `/dev/shm/ugripper/firmware_status.json`，Web 仅读取快照；夹爪复用 HMI 驱动在空闲时读取，启动/重连自动刷新，失败每 30 秒重试且每次连接最多 3 次，录制期间不查询；配置页支持空闲时查看左右编码器原始计数、角度与弧度；每次位置查询在录制/维护共享锁内打开并关闭串口，只发送 Modbus 读命令，支持 1Mbps/115200 且不改设备配置。四个环境参数（语言、编码、胸部主摄开关、电源频率）提供下拉配置，确认后由 root 专用 helper 限定写入 `/etc/environment` 并重启数采。录制与维护/下载共用 `/run/ugripper/maintenance.lock` 原子锁，持有到各自收尾结束；主摄升级固定调用包内 dfu-util。配置及测试说明见 `standalone/UgripperWebServer/README.md`。
- Web 配置页时间同步测试：可配置每组录制 1–3600 秒、1–100 组、毫秒表延迟 0 秒起且小于录制时长（默认 100 秒/1 组/延迟 2 秒）。复用包内毫秒表脚本，经 root worker 自动控制 GPIO139，每组前后高电平清零 5 秒，START/STOP 与 GPIO 实测边界写入 episode 的 `stopwatch_timing.json`；与生产压测/维护/下载互斥，断开网页继续执行，取消等待收尾，中断不自动续跑。采集完成不代表已通过光学同步标定。
- 数据目录：`/mnt/data_disk/<device_sn_lower>/data`
- 持久化标定目录：`/etc/ugripper/config/calibration`
- 日志目录：`/var/log/ugripper`
- 运行日志镜像目录：`/mnt/data_disk/logs`
- 每次停录收尾会将 systemd journal（系统服务、内核、USB 等）增量保存到 `/mnt/data_disk/logs/system_<device_sn>_<YYYYMMDD>.log`；游标保存在日志目录，支持重启后续写。journal 清理、游标失效或恢复读取时会按完整日志与已有内容去重后原子替换，避免重复；保存失败不影响停录。
- 终端硬件检查：`/usr/local/bin/hws`。HWS 与 Web 概览均显示 Ego 绑定状态、连接状态和绑定序列号，共用只读快照解析；绑定以 `/dev/shm/ugripper/ego_binding.json` 为准，连接要求状态快照序列号匹配且 15 秒内更新，否则显示未知。未绑定时连接显示未检测，不额外运行 ADB，也不把 USB 存储枚举当作 Ego 在线。
- U 盘升级入口：`auto_update/usb_auto_update.sh`

双目起录等待 MCAP 打开后再允许编码/IMU 提交。停录先禁止新的会话提交，通过共享/独占锁等待在途编码与 IMU 提交结束，刷新生产者本地批次并关闭视频输入；MCAP 写线程排空当前会话的本地批次与队列后关闭文件并确认，下一轮才可开始。进程退出也须等待生产者全部结束后让 MCAP 写线程退出，避免只因队列暂空而提前关闭。SDK 采集线程不持有编码收尾锁。

## 3. 模块总览
| 模块 | 入口 | 当前职责 | 关键输入/输出 |
| --- | --- | --- | --- |
| systemd 主服务 | `pack_script/ugripper.service` | 以 `ubuntu` 用户拉起录制服务 | `/opt/ugripper/run_record.sh` |
| 薄壳启动脚本 | `run_record.sh` | 切到安装目录、维护本地 `/tmp` 到 `/mnt/data_disk/logs` 的增量日志同步，再拉起 `record_runtime`；数据盘等待由 runtime 处理 | `/tmp/umi_sys_<sn>_<date>.log`、`/mnt/data_disk/logs/umi_sys_<sn>_<date>.log` |
| 主运行时 | `bin/UgripperRuntime/UgripperRuntime` | HMI 按键状态机、软件录制控制、LED 灯效、提示音、pre/post 音频、camera/sensor 子进程管理、停录校验、关机请求；SIGINT/SIGTERM handler 只设置原子停止标志，ego 与其他子进程统一由主循环退出后的正常收尾路径停止 | episode 目录、`/dev/shm/ugripper/umi_record_control.pipe`、`/tmp/umi_shutdown_request` |
| 安全 NTP 同步 | `time_sync/safe_ntp_sync.sh` / `ugripper-ntp-sync.service` | 开机或安装后只在非录制态执行一次性时间同步，优先使用板端现有 `sntp -S`，再 fallback 到 `ntpd -q -g` / `timedatectl`；同步完成、超时或录制开始后停止常驻 NTP 服务 | `/tmp/umi_recording.lock`、`sntp`/`ntp` |
| 相机录制 | `bin/CameraRecorder/CameraRecorder` | 负责主摄/触觉会话录制；默认同时启动 Fays 双目链路，`+nostereo` 变体不启动 Fays | 默认 8 路基础 `mkv`；`+nostereo` 为 6 路 |
| 传感器录制 | `bin/SensorRecorder/SensorRecorder` | 录制左右 IMU/encoder，按“采样入队 + 每侧独立 MCAP 写线程”分别输出 MCAP；收到 `SIGINT` / `SIGTERM` 时信号处理函数只设置停止标志，退出主循环后先等待编码器读写线程停止，再关闭串口、排空队列并关闭 MCAP，避免信号上下文日志死锁或串口释放竞态造成 Footer 缺失；encoder zeroing 使用相同的信号安全约束 | `sensor_left.mcap`、`sensor_right.mcap` |
| Ego 联动采集 | `bin/UgripperRuntime/ego/ego_recording_worker.py` + `bin/UgripperRuntime/adb/adb` | 通过左右上键组合绑定/解绑一台 SXR Ego；录制起停阶段通过内置 ADB 联动 Ego app，起录前同步系统时间并按增量方式同步 Ego episode。未绑定时保持可选联动；绑定后设备在线、停录 finalize、文件拉取和远端/本地大小核对均成为录制硬条件；绑定只在本次开机周期内有效，整机重启后自动解除 | `/dev/shm/ugripper/ego_binding.json`、`/dev/shm/ugripper/ego_binding_status.json`、`ego/`、`ego/ego_sync.json` |
| HMI 类库 | `standalone/GripperHmiTool` | 读取夹爪按键，并在驱动内部以单线程 owner 线程完成状态查询、灯效生成与 RGB 指令发送；默认由状态机切灯效，必要时仍可直接下发 RGB；`--read-version` 仅只读 HMI `0x52/0x56` 与 `0x52/0x57` 两路版本。显式 `--initialize-version-preserve-payload` 才会执行版本存储初始化，并在初始化前缓存完整 `32-byte` SN 与 `1024-byte` 标定 payload，随后原样写回、逐字节回读校验，失败最多重试 3 轮 | 按键快照、RGB 指令、双路版本、SN/标定参数读写与无损版本初始化 |
| 音频播放 | `bin/UgripperRuntime/audio/audio_play.py` | 优先绑定受支持 USB 耳机、无耳机时回退系统默认声卡；播放提示音并处理耳机 HID 音量键；初始化阶段受控处理 idle suspend | `/dev/shm/ugripper/umi_audio_pipe` |
| 音频采集 | `audio/record_usb_audio.py` | 优先从受支持 USB 耳机麦克风录音，无耳机时回退系统默认 source，供 pre/post 处理链路使用 | 临时 wav 文件 |
| 数据盘挂载 | `config/99-fixed-usb-map.rules` | 限定允许物理 USB 口，使用独立 mount helper 将数据盘挂到 `/mnt/data_disk`；主包只负责挂载/卸载，不再直接拉起 updater | `/mnt/data_disk` |
| 硬件健康检查 | `scripts/hws` / `/usr/local/bin/hws` | 一行命令列出左右手全部传感器、胸部相机与数据盘 symlink 的在线状态，便于现场快速确认是否全部在线 | 终端状态表 |
| USB 导入/升级 | `auto_update/usb_auto_update.sh` | 处理 `deb` 升级/重装、配置导入、标定数据导入、encoder 校准触发；当前会按固定名单自动安装 `das-usb-updater`、`ugripper`、`bluetooth-gatt-server`、`databot-device-joint`、`device-ota-mender`，只要 U 盘根目录存在名单内包就执行安装，同版本也会强制重装；deb 安装窗口会显示 `CALIB_RUN` 黄灯快闪且至少保持 `3s`，任一包失败显示 `ERROR_1` 红灯 `3s`，全部包处理成功显示 `CALIB_DONE` 绿灯 `3s`；全部安装完成后再通过 PulseAudio 播放 `upgrade_completed.wav`；其中标定导入会同时刷新主机侧主相机参数和夹爪侧 RGB/stereo/IMU payload。该脚本及其 `/usr/local/bin` + systemd unit 触发链归属可选独立包 `das-usb-updater` | `/etc/environment`、`calibration.json`、夹爪 HMI |
| 校准执行 | `auto_calibration/run_calibration.sh` | 在 `calibration.txt` 存在时停止业务、复用 `/mnt/data_disk` 触发左右编码器并行 zeroing、恢复服务 | `bin/SensorRecorder/zeroing` |

## 4. 启动链路
1. `pack_script/postinst` 在安装或升级 `ugripper` 时会尝试把 `/home/user/lib/exfat.ko` 复制到 `/lib/modules/<kernel>/extra/`，写入 `/etc/modules-load.d/ugripper-exfat.conf`，并在安装当次立即尝试加载 `exfat`。
2. 后续开机时，`systemd-modules-load` 会优先加载 `exfat`；允许的 USB 数据盘分区再由 `config/99-fixed-usb-map.rules` 按物理 USB 父链路匹配，在 `add/change` 事件里触发 helper 挂载到 `/mnt/data_disk`。
3. systemd 启动 `ugripper.service`。
4. 服务进入 `/opt/ugripper/run_record.sh`。
5. `run_record.sh` 读取 `/etc/environment` 中的 `DEVICE_SN`，建立当天运行日志文件名 `umi_sys_<device_sn_lower>_<YYYYMMDD>.log`。
6. `run_record.sh` 检查 `./bin/UgripperRuntime/UgripperRuntime`，然后等待 `/mnt/data_disk` 成为真实可写挂载点。
7. `run_record.sh` 在启动 Runtime 前建立 V3 主日志单写入口：所有业务及 child stdout/stderr 按物理行归一化后先落到 `/tmp`，再在视频停录、音频停录请求或运行时退出时增量同步到 `/mnt/data_disk/logs`，并清理本地与数据盘上同 SN 的非当天日志。
8. `run_record.sh` 再拉起 `record_runtime`；运行时退出前会额外补一次日志同步。
9. `record_runtime` 启动后读取 `/etc/environment`，至少关注：
   - `DEVICE_SN`：决定数据目录。
   - `CAMERA_CODEC`：非法值会回退到 `h264`。
   - `UGRIPPER_LANG`：决定提示音语言。
10. 运行时连接左右夹爪 HMI、启动 LED 渲染线程、尝试拉起音频守护进程。
- HMI 状态查询、LED RGB 与蜂鸣指令会在单个 gripper 串口内由驱动 owner 线程统一调度；`record_runtime` 只切换灯效模式，不再跨线程推送录制态的 500ms 亮灭边沿。所有周期灯效直接使用同一个绝对单调时钟计算相位，左右夹爪不会因状态恢复时刻或单侧重连而独立重新计时。当前驱动参考旧版 `driver_origin/led_manager` 的职责分离思路做了收敛：按键输入仍以 HMI 主动上报 `KeyReport` 为主，串口主动查询已降为约 `1s` 一次的低频探活/状态刷新；LED 仅在颜色变化、状态切换或低频补发时下发，避免高频状态查询与 RGB 指令互相抢占；蜂鸣开启和关闭状态切换都会以 `20ms` 间隔连续下发 `5` 次，开启后仍维持原有保活，HMI 连接建立和驱动退出时也会主动重发关闭状态。Ego 绑定、解绑和绑定失败反馈结束后还会按侧请求最新蜂鸣状态，只有回读 `duty/frequency=0/0` 才确认关闭，否则最多重试 `5` 次，避免最后一个关闭帧丢失后长期蜂鸣。若有专项诊断或直控需求，仍可走 direct RGB 通道覆盖当前效果。
- `standalone/GripperHmiTool` 当前新增了 UMI SN / 标定参数协议封装：SN 固定为 32-byte 字段（当前现场 SN 文本示例为 16-char，尾部补 `0x00`），标定参数固定为 `1024 byte` 严格对齐结构；当前 payload 已覆盖 RGB 主相机、双目 `cam0/cam1`、`cam->imu` 外参、IMU 离散噪声/随机游走与残差统计，其中 header 会保留内部有效数据长度，但当前 `V1.1` 固件写入时仍必须补满 `64 x 16B` 数据包，具体协议见 `docs/umi_calibration_protocol.md`。
- UMI 标定写入当前增加了异常恢复口径：若写入阶段收到 `0xFE`（当前 chunk 零数据校验错误），驱动会优先重发当前 chunk；若连续出现 `0xFE`，或后续读 SN / 读标定返回 `0xF3/0xFE`，则会发送一次 `AbortWriteInData` 清理固件残留写入状态后再重试。
11. 初始化成功后进入空闲状态机；默认包要求数据盘和 Fays recorder ready 才进入 `READY`。`+nostereo` 变体只要求数据盘与非双目关键设备 ready，不等待 Fays。
   - `UGRIPPER_ENABLE_STEREO` 是构建期开关，默认 `ON`；编译结果通过 `PACKAGE_STEREO_ENABLED=true/false` 日志体现。
   - 该开关不能通过 `/etc/environment`、`config.txt` 或命令行覆盖。
12. 默认包启动并维护 Fays stereo daemon、左右 recorder、FIFO 与热插拔恢复；`+nostereo` 变体保留相关软件资产，但不会构造或启动这条链路。
13. `record_runtime` 当前按 recorder 进程组而不是单一父 PID 回收 `camera_recorder` / `sensor_recorder`；当停录或异常收尾时，会向整组发送退出信号，降低内部 `ffmpeg`/`gst` 子进程残留导致后续卡死的概率。
14. `ugripper-ntp-sync.service` 属于独立 oneshot 辅助服务：安装后和开机后异步启动，不阻塞 `ugripper.service` 主链路；脚本先停止 `ntp.service` / `chrony.service` / `systemd-timesyncd.service` 这类常驻校时服务，再检查 `/tmp/umi_recording.lock`，录制中直接跳过，空闲时优先用 `sntp -S` 做一次性校时，再 fallback 到 `ntpd -q -g` 或 `timedatectl`。同步完成、超时、失败或检测到录制开始后都会再次停止常驻 NTP 服务，避免录制时间轴被系统时间校准跳变影响。

## 5. 状态机与按键行为
### 5.1 空闲态与阈值
- `READY`：绿色呼吸灯，提示系统可开始录制。
- 当前 `READY` 呼吸灯周期约 `4.5s`，LED 渲染线程约每 `20ms` 按单调时钟刷新一次；驱动只在亮度实际变化时发送 RGB，并以约 `250ms` 的低频做灯效补发、约 `1s` 的低频做串口探活，降低肉眼可见抖动和录制态丢闪。
- 主循环轮询周期约 `20ms`。
- HMI 按键原始状态在进入动作状态机前，对按下和松开分别执行 `40ms` 稳定时间滤波；只有状态连续稳定满阈值才更新有效按键状态。短按事件之间仍保留 `80ms` 动作间隔限制，两层机制分别处理电平抖动与重复动作限频。
- 普通单键长按判定阈值为 `1000ms`；录制中右手上键任务切片使用独立的 `800ms`；左右上键 Ego 组合键需同时保持 `2000ms`。右手双键关机提示阈值为 `2000ms`，左右同手双键执行阈值均为 `4000ms`。

### 5.2 按键动作

| 按键/组合 | 触发条件 | 空闲态 | 录制态 |
| --- | --- | --- | --- |
| 右上短按 | `1s` 内同键双击 | 开始普通录制 | 停止当前录制 |
| 右下短按 | `1s` 内同键双击 | 有上一条 episode 时开始 reset 录制；否则播报 `no_reset_needed` | 停止当前录制 |
| 右上长按 | 普通 `1000ms`；录制标记 `800ms` | 录制 pre audio | 第一次写 `task_start_s`，第二次写 `task_stop_s`，第三次及以后拒绝；有效标记由双手同步显示一、二次青色短闪并保留右手蜂鸣，第三次由右手显示三次橙色长闪并蜂鸣 |
| 右下长按 | `1000ms` | 录制 post audio | 忽略 |
| 左上长按 | `1000ms` | 将上一条完成 episode 标记为 `operator_marked_failed`，写 `validation_error.log`，并用紫灯与双手两次蜂鸣反馈 | 忽略 |
| 左下长按 | `1000ms` | 预留，不执行动作 | 预留，不执行动作 |
| 左上 + 右上 | 两键同时保持 `2000ms` | 未绑定时扫描并绑定唯一一台符合型号条件的 SXR Ego；已绑定时解除绑定 | 禁止绑定/解绑，右上仍按任务标记语义处理 |
| 右上 + 右下 | `2000ms` 提示，`4000ms` 执行 | 播放 `shutdown` 提示后进入 `EXIT`，写系统关机请求 | 必要时先停录，再执行同一关机流程 |
| 左上 + 左下 | `4000ms` | 进入 `writing`、刷写日志并请求卸载 `/mnt/data_disk`；成功播放 `umount`，失败播放 `error` | 忽略并播报 `error` |

左右上键采用精确按键掩码路由：两键形成组合后会抢占并重置各自的单键状态，额外按下左下或右下时不匹配该组合；组合执行后必须全部释放才允许下一次动作。这样第二键可在普通长按的 `1000ms` 容错窗口内加入，而不会同时触发 pre audio、人工失败标记或短按录制动作。组合绑定成功为双手三次亮黄色短闪，并依次播放 `1000/2000/4000Hz` 升调；解绑成功为双手三次紫色短闪，并依次播放 `4000/2000/1000Hz` 降调；绑定失败保持双手三次红色长闪与三次长蜂鸣。三类反馈结束时都会逐侧回读确认蜂鸣器已关闭，再恢复当前 `READY`、`RECORDING` 或错误灯效。

Ego 绑定文件位于 `/dev/shm/ugripper/ego_binding.json`：同一次开机周期内重启 `ugripper.service` 或重装软件仍保留绑定，整机重启或断电开机后 `/dev/shm` 清空，因此自动恢复为未绑定状态。`ego_binding_status.json` 使用 `bound / connected / disconnected / unbound` 表示绑定完成、在线、离线和已解绑；服务启动时若没有有效 binding，会主动发布 `unbound`，清理旧版本遗留的 `connected / disconnected` 快照。解绑不依赖 ADB 在线，即使 Ego 已拔出并进入 `disconnected / ERROR_5`，仍可通过左右上键组合解除绑定并清除该故障。解绑会在共享锁保护下删除 binding 并原子发布不带旧 serial 的 `unbound`，在途的旧 probe 在发布结果前会重新核对当前 binding，不能把状态覆盖回旧 serial 的 `connected`。解绑后不再探测 Ego，因此随后物理拔出不会改变 `unbound`。安装新版本时会清理旧 `/var/lib/ugripper/ego/binding.json`，避免历史持久化绑定被重新加载。

Ego 绑定时通过指定 serial 的 `dumpsys package com.ssnwt.helloxr` 读取 `versionName`；数字版本达到 `1.0.4` 才允许电流配置，版本缺失、无法解析或低于门槛时跳过新增功能并保留绑定。版本支持普通三段版本及可选 `v/V` 前缀，带其他后缀的版本保守视为未知。绑定文件追加 `app` 与 `input_current`，记录版本、能力、电流配置及广播回读结果。电流直接读取 UGripper 主机 `/etc/environment` 的 `HARDWARE_VERSION`，仅 `2.5.x > 2.5.0` 设置 `1600000 µA`（1.6A）；缺失、非法、`<=2.5.0` 和其他系列统一 `1100000 µA`（1.1A），不使用 metadata 的 `UGRIPPER_HARDWARE_VERSION` 默认值。绑定时发送显式 `InputCurrentLimitReceiver / SET_INPUT_CURRENT_LIMIT` 广播，并核对 JSON 的状态、callback、requested/readback、matched 和 persisted；失败只告警，不取消绑定，也不改变录制硬条件。

Ego 语音目前仅适配 `1203`（存储容量不足，请及时清理）：启动等待存储和运行中健康检查沿用 `disk_full` 判定（数据盘可用空间为 `0`），首次检测到时异步发送 `SpeechCommandReceiver / SPEECH_PLAY`；持续同一次故障只尝试一次，退出 `disk_full` 后再次进入才重新触发。未挂载、只读、空间信息读取失败等其他磁盘故障不播这条。已绑定时仅使用该 serial，未绑定时仅选择唯一在线且符合 SXR 型号规则的 Ego；播报前重新读取 App 版本，低于 `1.0.4` 或未知则跳过。语言读取主机 `/etc/environment` 的 `UGRIPPER_LANG`，缺失时回退进程环境，`en` 使用英文，其余使用 `zh`。ADB 超时或 App 拒绝只写告警，不阻塞磁盘故障处置；日志的 `accepted` 仅表示 App 接受入队，不表示已经播放完成。人工标记无效不再触发 `1103`，数据写入阶段不触发 `1003`。


## 6. 录制生命周期
### 6.1 开始录制
1. 在 `/mnt/data_disk/<device_sn_lower>/data` 下创建新的 `episode_YYYYMMDD_NNNN-temp`；目录名带 `-temp` 表示采集中或停录收尾中。
2. `metadata.json` 不在起录阶段预写；停录完成媒体 finalize、强校验和轻量探测后，才一次性写入最终结构，确保 `collection_duration_s`、`video_details[].duration_s/fps/start_offset_us` 与质量检查结果来自本次最终数据。
3. 一进入起录流程就写入 `/tmp/umi_recording.lock`，内容包含 `record_runtime` 的 `pid`、episode 目录（创建前可为空）与起录时间；episode 目录创建后会更新锁内容。该锁会覆盖 episode 准备、recorder 启动、录制、停录写盘和校验阶段，供 NTP 同步等系统级辅助动作避让录制窗口。若准备或启动失败会立即清锁；停录收尾完成后清锁。
4. 写入 `calibration.json`：当前不再从旧持久化 `calibration.json` 迁移或清洗后输出，而是按运行时设备缓存、Fays daemon 标定状态和 tactile USB serial 运行时缓存从零组装，避免旧标定字段、旧命名或旧表述混入 episode。`calibration.json` 的输出格式当前已锁定：
   - 这是锁定格式，顶层字段集合不得增加，已有字段的职责不得漂移；若必须调整，必须先更新本节文档，再同步修改生成代码、持久化刷新逻辑与 episode 校验口径。
   - 顶层只保留 `calibration_info/observation`。
   - `calibration_info` 按顺序输出 `calibration_status/main_cam_extrinsic_calib_status/format_version`，其中 `format_version=3.1`；主摄外参状态位于第四行，左右两组矩阵均有效才为 `calibrated`，否则为 `uncalibrated`，不改变原内参/Fays 状态口径。
   - `observation.images` 使用新范本 key：`cam_left_main/cam_right_main/cam_chest_main/stereo_left/stereo_right/tcam_left_l/tcam_left_r/tcam_right_l/tcam_right_r`。
   - `observation.imu` 使用 `imu_left/imu_right`。
   - 主摄字段保留 `camera_model/distortion_coeffs/distortion_model/fps/intrinsics/names/shape`；左右主摄增加 `extrinsics.T_base_main_camera/T_main_camera_stereo_cam0`，胸部主摄不增加外参。内参缺缓存时写默认占位，停录强校验再判失败。
   - tactile 字段只保留 `names/serial/shape`，serial 来自运行时在 `/dev/tcam_*` 插入或 symlink 目标变化时维护的 USB sysfs serial 缓存；episode 生成只消费缓存，不再停录阶段额外执行 `udevadm` 探测。
   - stereo 字段按范本保留 `cam0/cam1/camera_model/distortion_model/extrinsics/fps/names/shape`；IMU 字段按范本保留 `accelerometer/gyroscope/update_rate_hz`。
   - 左右主摄、胸部主摄 SN 与主摄标定参数由运行时在相机 symlink 插入/目标变化时通过 Yuzhou UVC XU 异步刷新缓存，拔出时清空；episode 生成只消费缓存，不再停录后同步读 XU。若在线主摄缺少合法 `FE...` SN 或 `MCAL` 标定 payload，本条 episode 会在停录校验阶段失败。
   - 左右 stereo 与 IMU 标定由 Fays daemon 状态填充；主摄外参来自夹爪 UCAL 缓存：V2 bit6/0x1B0 对应 `T_main_camera_stereo_cam0`，V3 bit7/0x1F0 对应 `T_base_main_camera`，均直接使用存储矩阵，不求逆。空闲时通过 HMI owner 驱动读取，失败每 30 秒重试、每次连接最多 3 次，连接变化清除旧缓存，录制期间不查询。矩阵逐项校验版本、长度、有效位、有限数值和刚体结构；缺失项使用约定的默认结构外参，保留另一项有效实读值；不从旧持久化 calibration 补值。
5. `record_runtime` 不再产出最终 `info.json`；camera/stereo 录制链路只在 `/dev/shm/ugripper_recording_timing_*.json` 维护内部 timing 缓存，用于 stereo offset 合并、并行视频探测结果缓存和最终 `metadata.json.video_details[].start_offset_us/duration_s` 生成，episode 完成 rename 前会清理该内部文件。停录硬校验只面向最终媒体、MCAP、`calibration.json` 和最终 `metadata.json` 语义，不再把该 shm 缓存作为 episode 产物或硬校验对象：
   - 内部文件顶层临时保留下面这些字段，其他临时调试字段不得写入。
   - `boot_time_offset`
   - `boot_time_offset_us`
   - 8 路基础 `<camera>_record_time_offset_us`，启用 `chest_cam_main` 时额外包含该路 offset。
   - 默认 stereo 额外缓存 `<camera>_effective_duration_us / <camera>_camera_frame_count`；Fays finalize 后，runtime 通过现有 MCAP reader 读取 summary、首 chunk 和尾部 chunk，取得 camera 首末 `publishTime` 与消息数。最终 metadata 只消费该缓存，不重复读取 MCAP；这些内部字段不进入最终 metadata schema。
   - `video_probes`：停录校验阶段并行 `ffprobe` 后写入的内部缓存，仅供同次 `metadata.json` 生成复用，不进入最终 episode。
   - 若现场日志出现 timing 打开失败，优先按日志里的真实 `/dev/shm/ugripper_recording_timing_*.json` 路径排查；旧字符串 `.recording_timing.json` 只保留为兼容性提示，不代表当前会在 episode 目录生成该文件。
6. 若已准备 pre audio，则移动到本次 episode 的 `audio_pre.wav`。
7. Ego 未绑定时，起录前 `record_runtime` 保持原可选联动：先用随包 ADB 做短窗口在线设备预检，明确无在线设备时直接跳过 sidecar；预检发现设备或结果不确定时拉起 worker，短等待内未拿到远端 episode 只记录告警，不阻塞 UGripper。Ego 已绑定时只允许使用持久化 serial，绑定设备不在线或未成功拿到本次远端 episode 会阻止起录并进入 `ERROR_5`。worker 默认优先使用随包安装的 `bin/UgripperRuntime/adb/adb`：设备识别要求 `ro.product.manufacturer=SXR` 且 `ro.product.model/ro.product.device/ro.product.name/ro.build.product` 均为 `SXR_1`。起录前通过 `cmd alarm set-time` 将 Ego 绝对时间设为 `host_now + 20ms`；随后读取三次 Ego Unix 毫秒时间，以最低 RTT 样本的主机请求中点估算 offset，严格要求绝对误差 `<15ms`。未达标时仍按固定 `+20ms` 策略重新绝对授时，最多重试三次，不把上一轮残余 offset 直接反向叠加，避免 ADB 生效延迟变化导致正负振荡；任一轮达标立即继续。固定补偿可通过 `UGRIPPER_EGO_TIME_SYNC_SET_AHEAD_MS` 调整，`ego/ego_sync.json.attempts` 保留每次 `set_ahead_ms`、offset 和 set-time 结果。连续四次尝试仍不达标时，worker 不发送 `START_RECORDING`，写入 `time_sync_failed`/`ego_time_sync_failed`，输出带 `UGR-82-02` 的 V3 错误日志；无论 Ego 是否已绑定，本条 UGripper 起录都会终止、清理录制锁并进入 `ERROR_5`，本机 camera/sensor 不会继续录制。校时通过后才按 `CAMERA_CODEC` 广播 `SET_VIDEO_CODEC`（`h264 -> avc`，`h265 -> hevc`），再向已运行的 `com.ssnwt.helloxr` app 广播 `START_RECORDING`。worker 从 app external-files 根目录发现新建的 `episode_*-temp`：显式配置的 `UGRIPPER_EGO_REMOTE_ROOT` 优先级最高；未配置时把绑定 serial 的新版 `/files/dataset/<ego_serial>/data`、旧版 `/files/<ego_serial>/data/dataset`、legacy `/files/dataset` 与 fallback 扫描结果统一汇总去重，再从全部候选 root 查找本次新增 episode，避免空的旧目录遮蔽实际写入路径。远端文件增量同步到本条 episode 的 `ego/`，当前仍不主动启动 Ego app。
   - 240 实测依据：`-10..60ms` 八档补偿各 6 次的梯度拟合为 `offset=0.9858*补偿-18.885ms`（`R²=0.9694`），对应有效生效延迟约 `19.2ms`。部署固定 `+20ms` 实现后连续 50 轮均首次授时达标，随后独立复测也 `50/50` 满足 `<15ms`；同步平均 `112.0ms`、P95 `127ms`，内部绝对误差均值 `3.57ms`、P95 `7.05ms`、最大 `8ms`，独立复测最大 `9.5ms`。
8. 并行启动：
   - `camera_recorder --codec <codec> --output-dir <episode> --only left_cam_main,right_cam_main[,chest_cam_main],left_tcam_l,left_tcam_r,right_tcam_l,right_tcam_r`
   - `sensor_recorder <episode_dir>`
9. 默认包同时向左右 Fays recorder 发送 session `START`；`+nostereo` 变体跳过该步骤。
10. 切换到 `RECORDING` 状态并播放开始提示音。

### 6.2 相机链路
- 配置入口：`config/camera_recorder.yaml`。
- 当前 YAML 定义 9 路相机：左右主摄、可选胸部主摄、左右 stereo、4 路触觉。
- `udev` 口位策略当前口径：
  - stereo 允许同侧 hub 的内部端口 `.1/.2` 互换；夹爪 CH9344 兼容 hub 内部端口 `.1/.2` 与四口 hub 的 `.4`，同组序号最小两路依次映射为 `*_gripper`、`*_encoder`；
  - 四口 USB2 hub 的固定映射为 `.1 -> cam_*`、`.2 -> tcam_*_r`、`.3 -> tcam_*_l`、`.4 -> CH9344`；夹爪 CH9344 只接受 bus `1` 作为右手、bus `9` 作为左手，bus `5/11` 保留给测试或扩展设备且不生成 `left/right_gripper`、`left/right_encoder` 链接；
  - tactile `l/r` 仍按左右侧固定 kernel 路径命名，不做跨侧互换；
  - 左侧更换新 hub 后，左主摄与左触觉 `l` 不再只按 `.4.2/.4.4` 固定口位判断，当前优先按设备类型识别：`27c2:0530` 归 `/dev/cam_left`，`0bda:5846` 归 `/dev/tcam_left_l`，USB2 口位仅用于限定属于左侧链路；
  - 左手新 hub 的 tactile 口位与右手保持同构：`.3 -> /dev/tcam_left_l`、`.4 -> /dev/tcam_left_r`；同时继续兼容旧特殊 hub 的 `/dev/tcam_left_r <- .3`。
- 默认运行时录制 8 路：左右主摄 + 左右 stereo + 4 路触觉；启用胸部主摄时为 9 路。`+nostereo` 变体为 6/7 路。
- 普通 `camera_recorder` 直接启动主摄与触觉；默认包另行启动双目 warmup daemon，`+nostereo` 变体不启动。
- 夹爪热插拔恢复完成后，`record_runtime` 当前会在该侧关键设备全部 ready、并完成 gripper runtime refresh 之后，只按 tactile 相机 USB `serial` 标记该侧实时参考帧待更新；服务初始化阶段不会直接打开触觉相机抓参考帧。待更新实时参考帧统一由后续首个可用 episode 的触觉视频截帧生成，若该 episode 缺失、损坏或截帧失败则顺延到下一条 episode。持久化 baseline 不因夹爪重连刷新，避免覆盖关机期间发生的盖板损坏。
以下 stereo warmup、session writer 与热插拔内容适用于默认包；`+nostereo` 变体不会执行：
- 当前随包 vendored Fays VIKit SDK 为 `3.8.3`，来源为上游 tag `v3.8.3` 提交 `8e2d91a08b065bde247a2283ecaa4bb7fd3aa419`；保留 `3.8.2` 对 IMU 帧时间戳小概率回退的修复，并优化数据流初始化稳定性。aarch64/x86_64 的 `libfays_vikit.so` 与版本头保持同一上游版本，FT602 运行库仍为 `1.0.17`。具体来源记录见 `src/third_party/fays_lib/UPSTREAM.md`。
- warmup daemon 常驻消费左右双目 `MJPEG 60fps` 预热流，并在录制时建立 session writer；主摄在普通录制阶段直接采集并写入最终文件。
- warmup daemon 当前按单实例口径运行；若服务内 daemon 尚未退出又手工再起第二个 `camera_recorder --stereo-daemon` 去抢同一批双目设备，可能诱发设备忙、节点缺失或整条 USB 链路重枚举。当前实现已增加 `/tmp/umi_camera_warmup_daemon.lock` 单实例锁，第二个 warmup daemon 会直接拒绝启动。
- 停录阶段也会并发向各路相机子进程发 stop，并在全部 stop 返回后统一 poll 状态，降低多路顺序收尾导致内部 timing 文件缺失或容器未 finalize 的风险。
- `camera_recorder` 当前对 ffmpeg 子进程采用统一的正确口径：录制器对象逐路启动，但每路采集/编码仍在各自子进程或内部线程里并发运行；普通触觉/主摄 shell recorder 与 stereo session ffmpeg 都会保留独立进程组，供正常 stop 路径按组发信号；同时启用父进程死亡自动终止保护，避免 `camera_recorder` 本体异常退出后遗留孤儿 `ffmpeg` 长时间占住 `/dev/left_tcam_*`、`/dev/right_tcam_*`，也避免从短生命周期启动线程里 `fork()` 导致 `PR_SET_PDEATHSIG` 被误触发。直接 `fork/exec` 的子进程在标准流重定向完成后会关闭其余 fd；Fays daemon 启动左右 recorder 时也会关闭 daemon 顶层控制 FIFO，使 recorder/ffmpeg 只持有自身实际需要的本侧 Fays 设备、控制与输入输出资源，不继承 CH9344、其他相机或其他侧硬件句柄。
- 主相机模式是压缩码流直封装：主摄始终走相机原生 `H.264/H.265` 码流，不做二次编码；当前实现已收口为 `camera_recorder` 进程内的 `V4L2 MMAP capture -> appsrc -> h26xparse -> matroskamux -> filesink`。`VIDIOC_STREAMON` 后会先检查压缩 access unit 的 NAL 结构，只在读到包含参数集与 IDR 的 clean keyframe 后开始写入 `appsrc`，超出最大等待帧数或约 `1.5s` 仍未满足则明确报错；若 EOS 已在轮询路径中被消费，停录线程会识别该状态并跳过重复 bus 等待。
- H.264/H.265 主相机在 STREAMON 前向 XU 写入 Unix 时间 `Tset`，GET 读取固件在 SET 时锁存的 encoder `Pset`；每个 access unit 的 XU2 SEI 提供当前校时基准 `Tset` 和当前帧 `Pframe`，逐帧 Unix 时间按 `Tset + Pframe - Pset` 计算。首个通过 clean keyframe startup gate、实际送入 `appsrc` 的帧固定为 `PTS=0`，`<camera>_record_time_offset_us` 使用该首关键帧换算后的 Unix 时间；后续 `PTS_us=当前帧换算后的 Unix 时间-首帧 Unix 时间`。H.264 使用 NAL type 6 的 XU2 SEI，H.265 使用 NAL type 39/40，两者共用时间映射；H.264 固件尾置 SEI 在送入 h264parse 前移至本帧首个 VCL 之前，保留参数集及 NAL 内容，appsrc 声明 AU 对齐，防止 SEI 归到下一帧或停录产生孤立 SEI 包；固件缺失 XU2 时明确失败，不回退 V4L2 时间。
- H.264/H.265 主摄录制中按单调时钟每 `10s` 由采集线程复用 XU SET/GET 接口校时，保存本条录制全部 `Tset/Pset` 基准，按每帧 SEI 的 `Tset` 匹配，兼容校时前已缓存的旧帧；固件过渡帧可能携带新 `Tset` 但 `Pframe<Pset`，因此必须使用有符号差值 `Tset+(Pframe-Pset)`，通过分支计算差值幅度防止无符号下溢，并拒绝非正 Unix 时间或 int64 溢出，不能仅因 `Pframe<Pset` 判失败；视频首帧 offset 不变，新映射直接作用于后续 PTS，不做平滑。新旧映射在同一 encoder 时刻的修正量绝对值 `>10000us` 时输出 `main_rtc_sync_jump` WARN；每次时间回退输出 `main_timestamp_clamp` WARN，并以 `last_pts_us+1us` 保持输出严格单调。周期 SET/GET 失败输出 `main_rtc_sync_failed` WARN 并中止该路录制，避免使用不确定基准。每次首次观察到当前基准输出 `main_rtc_anchor_applied`；下一轮 10 秒校时检查时仍未观察到上一基准则以 `stage=sei_ack` 中止该路，避免旧基准无限沿用。`main_camera_xu_tool --parse-h264-timestamps` 与 `--parse-h265-timestamps` 可离线解析对应码流。
- 两种编码下，主相机真实丢帧、USB 卡顿或采集/编码时间洞都必须保留在 PTS 中，不能再按 `frame_index/fps` 压紧；相同或回退的异常帧时间只以 `last_pts_us+1us` 做最小单调钳制，并输出 `main_timestamp_clamp` 诊断。正常无 gap 时仍接近名义帧率，发生 gap 时播放器可能表现为保持上一帧，但不得为了播放连续性牺牲跨传感器时间对齐。
- 主相机链路当前使用可配置的 V4L2 MMAP 压缩缓冲池，`v4l2_buffer_count` 默认按 `16` 个 buffer 的折中口径运行，避免回退到历史上更容易触发主摄短时背压/坏流的 8-buffer 口径，同时降低 RK3588 上多路相机反复起停时的 CMA/连续 DMA 内存压力；`appsrc`/`queue` 仍在背压时阻塞等待，避免下游繁忙时主动丢弃编码包。
- 录制中 `CameraRecorder` 若在左/右/胸部主摄启动阶段拿到 V4L2/USB 硬错误（例如 `VIDIOC_*`、`Input/output error`、设备消失或超时），会在 `/dev/shm/ugripper_camera_recorder_fault_<parent>_<episode>.json` 写入一次性故障证据；`record_runtime` 读到后立即按主摄 V4L2/USB 启动故障归类到 `ERROR_2`，中断当前录制并触发现有 USB 复位流程，避免等到停录校验阶段才因 timing/media 缺失落到 `ERROR_1`。主摄录制窗口达到约 `5s` 后，若输出帧跨度仍不超过约 `2s` 或帧数明显过少，也会写入 `main_camera_short_stream` 故障并按同一路径处理；用户在 `5s` 内快速启停不会触发短流判定。若 ioctl 卡在内核而应用层没有返回错误，`record_runtime` 还会只读检查 `CameraRecorder` 进程的 `/proc/<pid>/task/*/stat`；线程连续处于内核 `D` 状态超过 `5s` 时，同样按主摄 V4L2/USB 内核等待故障进入 `ERROR_2`。该检查不打开相机设备、不占用 V4L2 fd。
- 主摄 YAML 不再支持 `uvc_roll_absolute`。主摄厂商扩展控制工具 `main_camera_xu_tool` 与 `ensure_main_camera_packet_size_once.sh` 当前随包保留，但 udev 自动触发暂时停用；普通录制阶段不做 UVC 控制写入。`main_camera_xu_tool` 支持 `--read-sn` 仅读取 SN 所在 chunk、`--read-calib`/`--validate` 仅读取当前 V1 标定结构所需前 10 个 chunk，用于快速验证 SN 与 `MCAL` 标定是否可读。
- 触觉 / 双目模式保留 `hybrid-decode-encode` / `stereo-hybrid-decode-encode`。
- stereo 当前默认按设备 `1280x400@60` 常驻采集 MJPEG，session writer 按 `30fps` 抽帧后再编码成 `H.265` 写入 `mkv`；当前不再依赖后台 live encode + UDP relay。
- 每路相机由独立子进程承载；单路失败不会由 `camera_recorder` 主动连带停掉其他相机。
- stereo 热插拔语义：
  - 单侧设备缺失时只停止该侧 Fays recorder，保留另一侧 recorder 与 daemon 主循环；状态降为 `not-ready`，双手模式下仍要求左右都 ready 才允许录制。
  - 设备重新枚举后自动重建该侧 MJPEG warmup 取流并重新进入预热态；若掉线发生在录制中，则当前 stereo session 会被标记失败并在停录阶段显式报错，避免静默产出错位文件。
  - recorder 会低频刷新 `/dev/shm/umi_left_fays_runtime_status.json` / `/dev/shm/umi_right_fays_runtime_status.json`，记录最近 warmup/编码 frame 时间和当前录制 session id。该 status 只服务进程健康和会话状态，不承载 metadata 时间；停录 finalize 后由 runtime 轻量读取 Fays MCAP summary 与首尾 chunk 生成 timing 缓存，不做完整消息扫描。
  - 录制中若某侧 recorder 工作状态异常，daemon 会立即记录当前 session 的单侧 stereo 控制错误，停录阶段快速返回失败；finalize 等待视频/MCAP 文件期间也会被已知错误打断，不再等缺失文件触发完整超时。若该侧 Fays 相机/IMU symlink 本身识别不到，仍按关键设备缺失进入 `ERROR_2`；若设备已在线但 recorder/FIFO/control 链路失效，则进入 `ERROR_4` 并提示拔插对应侧夹爪，软件复位不作为有效恢复手段。
  - 每次拉起单侧 Fays recorder 前会先等待该侧 stereo/IMU symlink 解析目标稳定，随后默认再延迟 `3s`，可通过 `FAYS_STEREO_START_DELAY_SEC` 覆盖，避免设备刚枚举完成或 videoN 仍在漂移时被 SDK 过快打开。
  - 左右 Fays recorder 不并行拉起：daemon 会按左右 stereo symlink 当前解析到的 `/dev/videoN` 顺序启动，较小 videoN 视为更早插入/枚举的一侧；单侧启动后会等待 SDK handle 完成启动（FIFO 在线、calibration serial 可读且 warmup frame fresh）或默认 `10s` 超时，再拉起另一侧，可通过 `FAYS_STEREO_START_COMPLETE_TIMEOUT_SEC` 覆盖该等待窗口。
  - 单侧 recorder 清理优先走 FIFO `EXIT` 触发进程内 `Stop()`，该路径会唤醒视频、IMU、MCAP 和编码队列；若 SDK 线程仍未退出，再依次用 `SIGTERM` 和 `SIGKILL` 兜底，避免外层 daemon 因无界 `wait` 卡住。
  - Fays recorder 重启不会主动执行 USB `unbind/bind`、USB reset 或 udev 规则重载；它会关闭旧进程持有的 SDK/video/IMU handle，再重新拉起 recorder 并由 SDK 打开当前 symlink 指向的设备节点。外层 daemon 在启动/重启前会用 `fuser` 检查该侧 stereo/IMU 实际节点是否仍被占用，必要时会终止占用这些节点的进程；因此日志中可能看到 video port 占用清理，但这不是内核 USB 设备 reset。
  - Fays 相关关键事件统一使用 V3 外层，`[fays_stereo_daemon]` / `[fays_recorder]` 保留为 payload 标签；时间使用统一 Unix epoch 微秒，可直接与其他主日志来源排序。覆盖 recorder start/stop/restart、video port busy/forced cleanup、session start/stop/finalize error，以及 `fays_record_example` 的进程启动、SDK handle、控制 FIFO 和进程退出。
  - 最终 `stereo_left.mkv` / `stereo_right.mkv` 始终直接由录制会话写入单文件；不会在 episode 目录生成 `stereo_info.json`、`.concat.txt` 或后台 segment cache 文件。
- 内部 `/dev/shm/ugripper_recording_timing_*.json` 的时间字段由 `camera_recorder` 负责写出，而不是在停录校验阶段回填：
  - `boot_time_offset`
  - `boot_time_offset_us`
  - 6 路基础 `<camera>_record_time_offset_us`，启用胸部主摄时为 7 路
  - 若写出失败，`camera_recorder` 会区分记录“打开文件失败 / flush-close 失败 / 缺少哪一路 `record_time_offset_us`”，便于直接判断是 shm 写失败还是某路 recorder 未产出 offset。
- 保留的双目实现采用“后台 MJPEG warmup + 录制时编码写最终文件”，但当前包不会进入该路径，也不会生成 stereo timing 字段。

### 6.3 传感器链路
- `sensor_recorder` 固定录制：
  - 右手：`/dev/right_encoder`
  - 左手：`/dev/left_encoder`
- 新版不再接板载 IM648，因此 `sensor_recorder` 不再打开 `/dev/left_imu` / `/dev/right_imu`，也不再写 `imu_left` / `imu_right` topic。
- encoder 当前按“解析/读取后入本地队列 -> 主循环批量消费 -> 每侧写线程落 MCAP”的方式输出；主循环不再直接同步阻塞 `McapWriter::write`。
- 左右 `sensor_*.mcap` 现各自由单独写线程落盘，降低 chunk 压缩或磁盘抖动对采样节奏的反压影响；若写队列持续堆积，日志会输出 backlog warning 便于现场判断是否存在写盘瓶颈。
- 录制态的 IMU 超阈值检测当前直接下沉在 `sensor_recorder`：复用其现有 IMU 消费路径完成左右手独立的 `gyro`/`accel` 阈值、去抖、cooldown 与最短保持时长判定，不再把原始 IMU 样本转发给 `record_runtime`。
- `sensor_recorder` 与 `record_runtime` 当前只通过单向本地 `pipe` 交换轻量告警状态消息；`record_runtime` 不接收原始 IMU 流，只在收到“进入告警”状态变化时写一次 warning，并统一控制左右夹爪 HMI 蜂鸣。
- 输出拆成两份 MCAP：
  - `sensor_right.mcap`
  - `sensor_left.mcap`
- IMU / encoder 的 MCAP 时间戳当前默认沿用主机侧原始样本时间；若主循环一次从本地缓冲区取到多帧样本，则认为出现了缓冲区 burst，会以该批最后一帧的主机时间为锚点，按各自名义频率（IMU `200Hz`、encoder `1kHz`）向前回填这批样本的伪时间戳，尽量消除追赶帧导致的时间轴挤压。
- encoder 连接会优先在 `1Mbps` 下做 3 次快速验证重试（总验证窗口约 `150ms`，目标控制在 `200ms` 内），仍无响应才回退 `115200`；若 `115200` 可响应，则切回 `1Mbps` 后再次验证。

### 6.4 停止录制
1. `record_runtime` 并发停止普通录制模式下的 `camera_recorder` 与 `sensor_recorder`，降低两条独立链路顺序收尾带来的蓝灯等待。
2. 默认包同时发送 Fays `STOP` 并等待 stereo/Fays finalize；`+nostereo` 变体跳过这些动作。
3. 先发送 `recording_stop`，随后立即切到 `writing`；`writing` 会抢占仍在播放的上一条提示。
4. 若本条 episode 已拉起 Ego sidecar，则停止增量同步并完成 Ego finalize。STOP 广播最多发送 `3` 次，失败重试固定间隔 `100ms`，每次 ADB 状态、广播返回码和结果写入 `ego/ego_sync.json`；只要 Ego 已实际进入 recording，三次 STOP 全失败或设备消失都写 `ego_disconnected`，即使运行时绑定快照丢失也会使 episode 失败并进入 `ERROR_5`。finalize 后先按本地文件长度从 Ego 远端 offset 续传，落盘后核对全部远端/本地文件大小；不一致时重新检查远端数据并有限重试，远端缩小或本地大于远端时仅重拉异常文件。设备不可用归类 `ego_disconnected`，设备在线但远端文件缺失或重复拉取仍不一致归类 `ego_pull_incomplete`；绑定模式下两者均为录制硬失败。只有最终大小一致才尝试清理远端 episode，远端删除失败或 cleanup 跳过只记录状态，不覆盖主结果，也不单独使 episode 失败；不新增 MP4/MCAP 深度内容校验。
5. `writing` 阶段分阶段 flush 普通相机、左右 sensor、`calibration.json`、可选音频、ego 文件和最终 metadata；默认包还会 flush stereo/Fays 产物，`+nostereo` 变体不会。
6. 生成最终 `metadata.json` 后执行稳定校验；视频探测结果尽量写入 `/dev/shm` timing 缓存供 metadata 复用。
7. 停录硬校验完成后异步执行触觉实时 reference 与持久化 baseline 的单帧比较，不阻塞 stop 返回，也不反改本条 episode 的 `quality_check_status`。
8. 停录收尾完成后将 `episode_YYYYMMDD_NNNN-temp` rename 为最终目录；质量成功回到 `READY`，普通完整性失败进入 `ERROR_1`，关键非双目设备/HMI故障进入 `ERROR_2`，磁盘故障进入 `ERROR_3`，其他运行时异常进入 `ERROR_5`。

## 7. Episode 产物与检查
### 7.1 默认产物
- 视频：
  - `cam_left.mkv`
  - `cam_right.mkv`
  - `cam_chest.mkv`（启用胸部主摄时）
  - `stereo_left.mkv`（默认包）
  - `stereo_right.mkv`（默认包）
  - `tcam_left_l.mkv`
  - `tcam_left_r.mkv`
  - `tcam_right_l.mkv`
  - `tcam_right_r.mkv`
- 传感器：
  - `sensor_left.mcap`
  - `sensor_right.mcap`
  - `fays_data_left.mcap`（默认包）
  - `fays_data_right.mcap`（默认包）
- 元数据：
  - `metadata.json`
  - `calibration.json`
- 条件产物：
  - `audio_pre.wav`
  - `audio_post.wav`
  - `validation_error.log`（校验失败时，记录失败原因）
  - `ego/ego_sync.json`、`ego/rgb.mp4`、`ego/tracking.mp4`、`ego/ctrl.mp4`、`ego/audio.m4a`、`ego/sensor.mcap`、`ego/calibration.json`、`ego/metadata.json`（检测到 SXR Ego 并完成或尝试联动采集时；绑定模式会强制检查拉取完成和文件大小一致，但这些文件仍不加入 `require_files`）

### 7.2 metadata.json
停录校验完成后，`record_runtime` 写入最终 `metadata.json`。当前顶层字段固定为：
`device_sn / device_type / device_mode / camera_codec / hardware_version / software_version / das_usb_updater_version / data_version / hardware_list / episode_name / data_uuid / audio_uuid / quality_check_status / quality_check_err_type / collection_duration_s / task_start_s / task_stop_s / boot_time_s / tactile_damage_sn_list / require_files / video_details`。

- `device_sn` 使用主控 `DEVICE_SN`，写出前统一转大写；`device_type=ugripper`，`device_mode=dual`，`data_version=3.2`。
- `boot_time_s` 是本次系统开机时间点的 Unix 秒时间戳，优先复用 CameraRecorder timing 中的 `boot_time_offset`；timing 缺失时按当前 Unix 时钟与单调时钟差值回退计算。
- `tactile_damage_sn_list` 初始写为 `[]`；停录后的后台触觉慢校验中，任意单次 finding 满足 `damaged=true` 就加入对应非空传感器 SN，不等待连续三次黄灯告警门槛，SN 去重后原子回写该字段；本条 episode 未发现损坏时保持 `[]`。
- `task_start_s` / `task_stop_s` 是录制中右手上键任务切片点的 Unix 秒时间戳，按触发顺序各只写一次；当前 `3.2` 录制端未标记时固定写 `0.0`。`boot_time_s`、`tactile_damage_sn_list` 依次紧跟在 `task_stop_s` 后，并位于 `require_files` 前。离线深度校验允许历史或外部数据省略两个 task 字段；只有有效 `task_start_s` 而没有有效 `task_stop_s` 时按“无结束前回环数据”处理，不降低校验状态。
- `metadata.json` 使用范本顺序写出顶层字段、`hardware_list` 字段和 `video_details[]` 字段；校验脚本会把 key 顺序漂移作为格式错误。
- `data_uuid` 使用系统随机 UUID；若 `/proc/sys/kernel/random/uuid` 不可用则回退 `libuuid` 的 `uuid_generate/uuid_unparse`。无音频文件时 `audio_uuid` 允许为空字符串。
- `hardware_version` 默认 `v2.5`，可通过 `UGRIPPER_HARDWARE_VERSION` 覆盖；`software_version` 来自 ugripper 主包版本并带 `v` 前缀；`das_usb_updater_version` 优先读取 `das-usb-updater` 包版本。
- `hardware_list` 保持固定 schema：左右 gripper、左右主摄、可选胸部主摄、四路 tactile 与左右 stereo 字段始终存在；默认包从 Fays 状态写 stereo SN，`+nostereo` 变体写空字符串。
- `quality_check_status` 取值为 `success` / `fail` / 空字符串；`quality_check_err_type` 保持单字段兼容，只写本次失败的主 `error_type`。常见取值包括 `missing_file`、`collection_duration_too_short`、`frame_loss`、`finalize_error`、`stereo_control_failed`、`calibration_error`、`runtime_error`、`device_disconnected`、人工补标的 `operator_marked_failed`，以及健康监控直接上报的 `disk_mount_lost`、`disk_not_writable`、`disk_full`、`critical_devices_missing`、`stereo_not_ready`、`hmi_*` 等 fault key；无法归类时写 `unknown`。
- `collection_duration_s` 取有效视频时长最大值；默认包包含 stereo，`+nostereo` 只统计主摄/触觉。stereo 的有效时长固定使用 Fays MCAP 中首末 camera `publishTime` 的跨度，因此 Fays 设备侧 gap 会保留在实际采集时长内，不使用被固定 `25fps` 压紧的 MKV 容器 duration；`video_details` 复用停录 timing 缓存。
- `video_details[].start_offset_us` 保存各路补偿后的绝对 `record_time_offset_us`，单位为 Unix 微秒，语义为“该视频 `PTS=0` 对应的补偿后主机 Unix 时间”，不再减去本 episode 最早视频 offset。H.265 主相机按 `start_offset_us + packet_pts_sec * 1000000` 还原 Unix 时间：首关键帧 `start_offset_us=Tset+Pfirst-Pset`，packet PTS 来自当前帧换算 Unix 时间减去首帧 Unix 时间，因此任意帧 Unix 时间等于 `Tset+Pframe-Pset`，并保留编码器时间轴上的真实 gap；H.264 主相机继续使用 V4L2 buffer 时间生成同一表达。触觉相机仍由 FFmpeg 录制链生成 PTS 和首帧 offset，目前只保证处于 Unix 时间域并执行单调/gap 校验，不把它表述为硬件曝光时间真值。
- stereo MKV packet 与 Fays MCAP `c` topic 通过 `frameIndex`/顺序一一对应；stereo 精确逐帧 Unix 时间只使用对应已补偿的 `c.publishTime`，`video_details[].start_offset_us` 仅与首个 camera `publishTime/1000` 对齐。Stereo MKV 保持固定帧率播放时间轴，设备 gap 不要求由 MKV PTS 表达，也不得用 `start_offset_us+stereo PTS` 替代 MCAP `publishTime`。Fays 原始 `logTime` 只用于设备侧 gap/回退分析，不能作为全局 Unix 时间。
- 默认包的 `require_files` 包含 stereo/Fays 文件；`+nostereo` 变体只包含 metadata、calibration、左右主摄、四路 tactile 与左右 sensor。启用胸部主摄时两种变体都追加 `cam_chest.mkv`。
- 离线 episode 深度校验以 `software_version` 中的 `+nostereo` 为构建 profile 标记，并交叉核对 `require_files`、`video_details` 与实际文件；普通版本缺失 stereo/Fays 仍判失败，不能通过删减 `require_files` 自动降级。当前标准版本为 `3.1`，历史 `3.0` 作为兼容数据允许分析，其他未知版本给出告警。

#### 7.2.1 相机时间戳与延迟补偿

相机延迟补偿按 recorder 模块分别维护：主摄与 tcam 的数值入口是 `standalone/CameraRecorder/include/camera_recorder/camera_timestamp_offsets.h`，stereo 的数值入口是 `standalone/FaysStereoRecorder/include/fays_stereo_recorder/camera_timestamp_offsets.h`。后续实验接受新的延迟值时，只修改对应头文件中的微秒常量并重新编译；业务代码不得再写独立的补偿数字。补偿值为正时统一加到对应的全局 Unix 时间上。

| 相机 | 原始时间来源 | 全局时间表达 | 补偿位置 | 当前值 |
| --- | --- | --- | --- | --- |
| 左右/胸部主摄 | H.265：XU `Tset/Pset` + XU2 SEI `Pframe`；H.264：V4L2 buffer 时间 | `start_offset_us + PTS` | CameraRecorder 写 `<camera>_record_time_offset_us` 时只调整绝对起点 | `0us` |
| 四路 tcam | recorder 首帧系统时间与 PTS | `start_offset_us + PTS` | CameraRecorder 写 `<camera>_record_time_offset_us` 时只调整绝对起点 | `-36,000us` |
| 左右 stereo camera | Fays camera 设备时间换算到 Unix | Fays MCAP `c.publishTime` | 图像采集后、进入 MCAP 队列前逐帧调整 | `-3,000us` |
| 左右 Fays IMU | Fays IMU 设备时间换算到 Unix | Fays MCAP `i.publishTime` | 不补偿 | `0us` |

H.265 主摄在 `VIDIOC_STREAMON` 前向 UVC XU selector `0x19` 写入当前主机 Unix 微秒 `Tset` 加 `200us`，优先使用板端实测有效的 unit `3`，并保留 README 原协议 unit `2` fallback；写入后立即 GET 固件锁存的 SET 时刻 encoder PTS `Pset`。码流必须为每个实际写入 access unit 提供合法 XU2 SEI，SEI 中 `xu_time_us` 必须等于本次 `Tset`，首个通过 startup gate 的 clean keyframe encoder PTS 冻结为 MKV 相对零点，且录制期间 `Tset` 不得变化。任意帧 Unix 时间按 `Tset + Pframe - Pset` 计算；主摄后处理补偿当前为 `0`，数值入口仍保留，后续有新的实验结论时可单独调整。H.264 不含该 SEI，继续使用现有 V4L2 时间路径。
主摄打开设备后读取 `/etc/environment` 中的通用配置 `UGRIPPER_POWER_LINE_FREQUENCY`，支持 `50`/`60` Hz 并映射到标准 V4L2 `power_line_frequency` 控制；安装包在缺少该变量时写入默认值 `50`，已有值不覆盖。参数命名保持通用，当前仅主摄录制链使用，不改变录制 PTS 计算。

主摄和 tcam 的后处理补偿不会修改 MKV packet PTS、帧间 gap 或 duration。tcam 将主机观测时间向前校正 `36ms`；Stereo 补偿只将 camera topic 的 Unix `publishTime` 向前校正 `3ms`，原始设备域 `logTime`、Fays IMU 时间戳和 stereo MKV 固定帧率 PTS 均保持不变。`record_runtime` 从首个已补偿的 stereo camera `publishTime` 生成 stereo `start_offset_us`，因此 metadata 阶段禁止再次应用 `3ms`。

### 7.3 停录强校验
停录后当前按以下层次校验：
- 默认包执行 stereo session/finalize 和 Fays tail 检查；`+nostereo` 变体完全跳过这些检查。
- 在线主摄、胸部主摄必须已有合法运行时缓存：SN 必须为 Yuzhou XU 中的 `FE...` 字段，标定必须为合法 `MCAL` V1 payload。缺任一项即校验失败，并写 `quality_check_err_type=calibration_error`。
- 文件存在性按构建变体决定：默认包额外要求左右 stereo 与 Fays MCAP，`+nostereo` 不要求；其他主摄、触觉、sensor、calibration 和 metadata 均必须存在且非空。
- 内部 timing 缓存：停录收尾期间由 `/dev/shm/ugripper_recording_timing_*.json` 暂存 `boot_time_offset`、`boot_time_offset_us`、各路 `<camera>_record_time_offset_us` 和 `video_probes`，只用于生成最终 metadata；该文件不落到 episode，不作为硬校验对象，完成后清理。若 `mergeEpisodeInfo()` 提示 timing 打开失败，应优先结合 `camera_recorder` 的“wrote timing file / missing_record_time_offset_us / flush-close failed”日志一起判断根因。
- metadata 与视频可读性：最终 `metadata.json` 必须包含当前变体全部视频的 `start_offset_us/duration_s`；默认包包含 stereo，`+nostereo` 不包含。
- 全局首帧同步由仓库离线 pipeline `test/scripts/validate_episode_data.sh` 深度检查，不加入停录运行时硬校验；pipeline 将全部视频、本机 sensor MCAP 中实际出现的全部 topic、默认包左右 Fays IMU/camera，以及存在时 `ego/sensor.mcap` 中的全部 topic 首帧统一到 Unix 时间轴，以最早流为参考列出逐流相对误差，最大误差达到 `1000ms` 时判失败。普通 MCAP topic 已是 Unix 时间时直接使用 `publishTime`；Ego `head_pose/controller_pose/imu` 的 `publishTime` 属于设备单调时钟，校验器以 `ego/metadata.json.head_pose_details[name=head_pose].start_offset_us` 和 `head_pose` MCAP 首帧计算固定 offset 后统一换算，Ego 已是 Unix 时间的 `*_metainfo` topic 不重复换算；Fays 原始 `logTime` 只用于设备 gap/回退分析。
- 主相机时间轴验收除首帧表外，还必须检查 packet PTS 严格单调、重复/回退、实际 gap 和末帧覆盖；发生人为丢帧或短时停顿时，后续包的 `start_offset_us+PTS` 不得产生累计提前。首帧最大误差只表示各 recorder 的启动错峰，不能替代逐帧 PTS 校验，也不能被解释为硬件曝光同步精度。
- 时长合理性：每路启用视频都必须满足跨度阈值；默认包的 stereo 时长复用停录时由 Fays MCAP 首尾 `publishTime` 写入的 timing 缓存，并继续执行既有 Fays MCAP footer/head-tail 检查；`+nostereo` 跳过。
- 条件产物：若执行了 pre/post 音频录制，对应 wav 仍需存在。
- 触觉软校验：
  - 每路 tactile 只取起录附近单帧；预处理只裁掉左侧约 `18%` 光源区域，不裁上边、右边和下边。若同 `serial` 实时参考帧缺失或处于夹爪重连待更新状态，则用本帧建立参考帧并跳过本轮实时 damaged 对比，失败则顺延到下一条 episode。已有实时参考帧时，先做全局亮度/对比度校正和直接 residual 差分，再对 residual mask 执行 `3x3` 邻域投票与 `8` 连通域遍历；每个连通域只有包含至少 `8` 个“自身及周围 `3x3` 全部为异常”的实心核心像素才准入，最终也只按这些实心核心像素判定 damaged。因 `1~2 px` 细直线、曲线和贴边轮廓无法形成 `3x3` 实心核心，其整个连通域会被过滤；面积型损坏仍保留。单次异常不让当前 episode 失败，仅用于连续 `3` 个 episode 的损坏提示。
  - 同时还会与同 `serial` 的持久化 baseline 比较；若 baseline 缺失，则用本帧初始化 baseline 并跳过本轮 persistent 对比，失败则顺延到下一条 episode。已有 baseline 不会因为夹爪重连或时间到期刷新；persistent 比较独立维护最近 `3` 次窗口，连续 `3` 次异常才播放对应 damaged 语音并置位，并登记“下次重新开关机后允许刷新 baseline”。重新开关机后，下一条可用 episode 会刷新该路持久化 baseline；连续 `3` 次 clean 后清除告警但不刷新 baseline。
- 当前运行时轻量阈值口径：
  - 单个连通域的 `3x3` 实心核心像素数至少为 `8`，存在合格核心即判 damaged；`robust_residual_area` 保留为合格实心核心像素占整帧的比例，仅用于诊断兼容，不再使用旧的 `0.003` 面积门限
  - 动态残差阈值为 `max(10, median(residual) + 6 * 1.4826 * MAD(residual))`
  - 全局空间配准和边缘局部容差默认关闭（半径均为 `0`）；运行时直接跳过 blur/search/Sobel/local residual，避免为纹理边线误差额外消耗计算量
  - 当基线与当前帧平均亮度相差至少 `8` 时，额外尝试九分位单调亮度 LUT；只有 LUT 后残差 P99 不超过 `8.5`、相对线性校正下降至少 `40%` 且结构相关性提高时才采用，否则回退线性校正。该条件用于消除同一纹理的非线性曝光变化，不对局部光照场做拟合。
  - 基线兼容性使用全局相关性与 `10x16` 分块相关性共同判断：全局相关性低于 `0.90` 且匹配块比例不超过 `5%` 时，标记 `baseline_refresh_required` 而不作为损坏；全局相关低但仍保留超过 `5%` 的匹配块时按大面积局部结构损坏处理。运行时只记录该诊断，不自动覆盖基线，避免整面损坏被错误学习为新基线。
  - residual mask 过滤为：`3x3` 内异常像素数至少 `3`，且 `8` 连通域面积至少 `8 px`；随后计算 `3x3` 实心核心并过滤核心少于 `8 px` 的 curve 型连通域
  - 诊断日志记录 `solid_core_px / max_solid_core_component_px / curve_drop_px / curve_drop_components`，用于快速区分面积型损坏与曲线/边线误差。

- 单帧触觉算法交付：完整实现位于 `tools/tactile_damage_detector/`，目录内不调用仓库其他脚本；`scripts/package_tactile_damage_detector.sh` 会生成不含测试图片的 `tmp/tactile_damage_detector.tar.gz`。包内 `detect` 接收 `--baseline` 与 `--image`，输出 `result.json / mask.png / comparison.png`；普通原图沿用运行时左侧 `18%` 裁剪与 `160x120` 灰度预处理，`--preprocessed` 用于已预处理图片。返回码 `0/2/1` 分别表示 clean/damaged/运行错误；`baseline_mismatch` 以 clean 返回并在 JSON 中要求刷新基线，但工具不会自动修改输入。该工具只输出单帧结论，不包含连续 episode 黄灯决策。

说明：当前不会为视频做全量逐帧扫描；强校验只读取容器元信息并消费内部 timing 字段，触觉软校验也只做单帧快速比较，优先保证现场稳定性与停录耗时可控。

## 8. 灯效、音频与关键路径
### 8.1 当前状态灯语义
- `INIT`：初始化或等待 Fays recorder ready，蓝灯闪烁。
- `WRITING`：停录收尾、文件 flush、左手双键卸载数据盘等写盘/收尾阶段，蓝灯常亮。
- `READY`：可录制，绿色呼吸灯；从录制/写盘等其他状态切回 READY 时，左右灯以最大绿灯亮度约 `70%` 的上升段作为呼吸起点，避免先全灭再缓慢亮起。进入后继续按左右共享的单调时钟与同一相位偏移渲染，重复设置 READY 不会重置呼吸周期，单侧反馈恢复或重连也沿用当前相位。
- `WARNING`：触觉软告警，黄灯按侧别与传感器位置编码闪烁；当前用于同一 tactile serial 的实时 reference 窗口或 persistent baseline 窗口连续 `3` 个 episode 异常。故障侧夹爪显示黄灯：`*_tcam_l` 为一长一短，`*_tcam_r` 为一长两短，同侧两路都异常为两长；左右侧都异常时两侧分别显示各自编码，未异常侧保持 READY 绿呼吸。若后续连续 `3` 次 clean，相关窗口会清除告警并回到 READY。
- `RECORDING`：录制中，左右同步绿色闪烁；任务标记的双手青色提示结束后继续按全局单调时钟相位显示。当前只在亮灭边沿和低频补发时下发 RGB，避免高频重复写串口造成丢闪。
- `CALIB_PRE` / `CALIB_RUN` / `CALIB_DONE`：供 USB 导入、deb 安装与校准脚本复用；deb 安装窗口使用 `CALIB_RUN` 表示安装中、`CALIB_DONE` 表示全部包处理完成。
- `ERROR_1` ~ `ERROR_5`：红灯长短码。当前口径下，`ERROR_1` 用于完整性/校验失败，`ERROR_2` 用于关键设备/HMI/stereo 缺失或不活跃，`ERROR_3` 用于数据盘挂载丢失 / 不可写 / 写满，`ERROR_4` 用于 Fays 相机已识别但单侧 stereo/Fays recorder、FIFO 或控制链路失效，`ERROR_5` 用于其他运行时异常。同一条 episode 可同时存在多个内部 `error_type`，最终灯效只按优先级播报一个：磁盘类优先 `ERROR_3`，关键设备缺失优先 `ERROR_2`，stereo 控制失效为 `ERROR_4`，运行时异常为 `ERROR_5`，普通校验失败为 `ERROR_1`。硬件缺失类 `ERROR_2` 和控制链路类 `ERROR_4` 都会按侧别提示：故障侧夹爪闪烁对应错误码，另一侧红灯常亮；左右都故障则两侧一起闪烁；`ERROR_2` 无法归属左右侧时，两侧同步先闪一次完整序列，再红灯常亮相同时间并循环。
- `EXIT`：关机退出阶段。

### 8.2 关键持久化与临时路径
- 持久化标定：`/etc/ugripper/config/calibration/calibration.json`
- USB 标定导入阶段会在 `/etc/ugripper/config/calibration/.import_stage.*` 下生成临时 `calibration.json` 与夹爪 `payload bin`；由于 HMI helper 当前会以 `ubuntu` 用户运行，stage 目录需保持可遍历、payload bin 需保持可读，否则会出现“bin 已生成但 helper 无法读取”的写入失败。
- 运行时音频 FIFO：`/dev/shm/ugripper/umi_audio_pipe`
- 运行时软件录制控制 FIFO：`/dev/shm/ugripper/umi_record_control.pipe`
- 音频临时目录：`/tmp/umi_audio`
- 系统动作请求文件：`/run/ugripper/system_action_request`
- 系统动作结果文件：`/run/ugripper/system_action_result`
- Ego 开机周期绑定：`/dev/shm/ugripper/ego_binding.json`
- Ego 绑定/探测状态：`/dev/shm/ugripper/ego_binding_status.json`
- 数据目录：`/mnt/data_disk/<device_sn_lower>/data`
- 运行日志：`/tmp/umi_sys_<device_sn_lower>_<YYYYMMDD>.log`
- 数据盘日志镜像：`/mnt/data_disk/logs/umi_sys_<device_sn_lower>_<YYYYMMDD>.log`
- stereo daemon 状态：`/tmp/umi_stereo_camera_status.json`
- 左右 Fays recorder 控制 FIFO：`/dev/shm/ugripper/umi_left_fays_cmd`、`/dev/shm/ugripper/umi_right_fays_cmd`
- stereo daemon 顶层控制 FIFO：`/dev/shm/ugripper/umi_stereo_camera_control.pipe`（保留兼容入口，当前普通录制起停由 `record_runtime` 直接写左右 Fays recorder FIFO）
- 触觉传感器持久化状态：`/var/lib/ugripper/tactile_state`（包含 `reference/`、`persistent/`子目录与 `history.json`；`persistent/` 存放跨关机有效的长周期 baseline）
- `/mnt/data_disk` 只作为固定挂载点使用：安装阶段会预创建为 `root:root 0555`，业务不会把本地空目录当成数据目录；只有真实数据盘挂载成功后才允许继续启动录制服务。
- 运行日志维护使用 V3 口径：单一 writer 将 C++、Python、Shell 和外部 child 的每个非空物理行统一格式后写入 `/tmp`，在视频停录、音频停录和运行时退出时增量同步到 `/mnt/data_disk/logs`，并只保留当天同 SN 日志。

### 8.3 软件录制控制
- `record_runtime` 启动后会创建并非阻塞打开 `/dev/shm/ugripper/umi_record_control.pipe`，供本机受控脚本触发录制起停；若 FIFO 创建或打开失败，只记录告警并继续保留原有 HMI 按键控制能力。
- `test/src/gripper_disconnect_repro_sop/` 提供可单独打包的现场复现工具，主机侧 `./scripts/prepare_240_repro.sh` 会通过 SSH 下发板端 worker，并复用该 FIFO 做连续软件录制与错误现场抓取；系统/服务实时日志仍建议测试人员另开终端手动观察。
- FIFO 按行接收文本命令，当前支持：
  - `SHORT_UP`：严格模拟右手上键短按语义；空闲时起录，录制中停录。
  - `SHORT_DOWN`：严格模拟右手下键短按语义；空闲且存在上一条 episode 时重录，录制中停录。
  - `START`：仅空闲时接受，并映射到 `ShortUpPressed`；录制中会忽略并记录 `[CONTROL_DIAG] ... reason=already_recording`。
  - `STOP`：仅录制中接受，并映射到 `ShortDownPressed`；空闲时会忽略并记录 `[CONTROL_DIAG] ... reason=not_recording`。
- 被接受的软件命令会在同一主循环内复用 `handleShortUpAction()` / `handleShortDownAction()`，不经过物理按键 `1s` 双击防误触门槛，因此停录会进入与按键双击确认后的相同 stop、finalize、merge、validation、LED/audio 恢复路径；不要通过直接写 stereo FIFO、kill recorder 或删除 lock 文件来替代停录。
- 停录成功、校验失败和异常停录三个收尾分支都先执行 `sync_runtime_log("video stop")`，再清空当前 episode 状态并删除 `/tmp/umi_recording.lock`，确保释放录制锁前已触发运行日志增量同步。
- 控制入口带 `300ms` 防抖，单轮主循环最多处理 `8` 条命令，避免外部脚本重复写入导致 start/stop 连续抖动；同一轮中若已接受软件控制命令，会跳过本轮物理按键事件处理，降低叠加触发风险。
- 建议使用示例：
  - `echo START > /dev/shm/ugripper/umi_record_control.pipe`
  - `echo STOP > /dev/shm/ugripper/umi_record_control.pipe`
  - 如需严格模拟按键：`echo SHORT_UP > /dev/shm/ugripper/umi_record_control.pipe`、`echo SHORT_DOWN > /dev/shm/ugripper/umi_record_control.pipe`
- `test/scripts/board_stopwatch_record_test.sh` 是面向测试交付的 PC 端外部秒表联动入口：默认连接 `ubuntu@192.168.2.240`，不携带或安装 CH9344 helper，不覆盖板端文件，也不 reload/replay udev；在 GPIO 和 `START` 前校验四个现有 CH9344 逻辑链接各自只有一个声明者且活动 symlink 指向该声明者，但不硬编码物理 bus，接受板端正式 UGripper 已生成的有效映射，校验失败时不开始测试。当前秒表硬件合同固定为 GPIO139 低电平持续运行、高电平持续至少 `5s` 清零；GPIO 初始化通过 sysfs `direction=high` 原子设置输出方向与高电平，避免先写 `out` 产生低电平误触发，随后由单个 root Python helper 预打开 GPIO `value` fd。脚本在录制前先保持高电平默认 `5s` 完成清零，再写 `START` 开始正常录制，默认等待 `2s` 后拉低 GPIO 启动秒表；默认总录制时长为 `15s`，秒表因此运行约 `13s`。达到总时长后先写 `STOP`，再恢复高电平并保持默认 `5s`，使秒表停止并清零；停录后的保持时间不计入录制时长，`--reset-hold-sec` 可增大但不得小于 `5`。正式 start/stop 只在常驻 helper 内执行 `CLOCK_REALTIME/CLOCK_MONOTONIC_RAW` 取时、`write()`、再次取时，不在关键路径启动 `date`、`sudo` 或 `tee`；异常退出也会恢复高电平并保持完整清零时长。脚本只使用 runtime FIFO，不直接启停 camera/sensor/Fays 子进程；等待正常停录收尾和 episode rename 完成后，在最终 episode 中额外写入 schema v2 `stopwatch_timing.json`，保存 GPIO start/stop 写入边界、前后高电平保持时间、清零完成时间、录制命令时间和测得延迟。时间文件属于测试辅助证据，不进入 `metadata.json` schema、`require_files` 或运行时质量校验范围。

### 8.4 PERF 日志
- 停录收尾、文件 flush、视频探测和 episode validation 会输出 `[PERF]` 耗时日志，用于现场拆分蓝灯延迟。
- `[PERF]` 日志由编译包决定，默认发布包关闭；需要开启时在构建时定义 `UGRIPPER_ENABLE_PERF_LOG=1`。运行时不再读取 `/etc/environment` 开关；错误日志、校验失败记录和必要状态日志不受影响，Fays 事件也使用统一 V3 外层。`run_record.sh` 还会默认导出 `mpp_debug=0`、`mpp_log_level=2`、`mpp_syslog_perror=0`，让 CameraRecorder/Fays/ffmpeg 子进程继承 MPP error-only 日志口径，避免 `mpp_info`、`mpp_enc` 初始化配置行刷屏。
- validation 耗时当前按粗粒度输出：setup、并行视频 probe、并行 tail checks 与总耗时。其中 encoder/Fays tail checks 会作为 4 个只读任务并行执行，并统一打一条总耗时与详情日志；后台 tactile 慢校验另行输出 queued/start/end/applied/cancel 相关 PERF。

### 8.5 音频链路关键行为
- `audio/audio_play.py` 启动时按 `UGRIPPER_LANG` 选语音，并优先绑定 PulseAudio 中受支持的 USB 音频设备；当前兼容 `0020:0b21 (liyuany USB Audio)` 与 `0023:0b23 (liyuany USB PnP Sound Device)`。若无耳机，则回退系统默认 sink/source；耳机晚于服务启动才出现时，守护进程会先启动 FIFO 和监听线程，再在耳机出现后自动切回耳机。
- 提示音主线程通过 FIFO 收命令后使用 `pygame.mixer` 播放到当前选中的 PulseAudio sink；启动时只做一次短静音预热、每段提示音前补前导静音，不再维持常驻静音 keepalive。
- `audio/audio_play.py` 只有在真实绑定到一个可用的 PulseAudio 播放目标并完成后端初始化后才会写 `/dev/shm/ugripper/umi_audio_ready`；当前无论是受支持 USB 耳机还是系统默认声卡，都需要建好 backend 才会进入 ready，backend teardown 时会移除该标记，避免业务把“进程活着”误判成“提示音已可播放”。
- 提示音调度采用“后触发抢占前触发”的语义，不做排队串行；新的命令到达后会立即停止当前 one-shot 或 loop 提示，再播放最新命令。当前 `writing`、`calibrating` 属于 loop 提示，但同样会被后续命令直接打断。
- `record_runtime` 会在主循环内监测音频守护进程；若守护进程异常退出会按节流策略自动重拉起。正常情况下，音频守护进程会在“受支持 USB 耳机”和“系统默认声卡”之间自动切换，并在 `/dev/shm/ugripper/umi_audio_ready` 恢复后补发空闲态 `ready` 或当前阶段提示。
- 耳机运行中被拔掉时，音频守护进程不会退出，而是自动退回系统默认声卡；耳机重新插入并重新出现在 PulseAudio 后，会自动重新绑定回耳机，后续提示音恢复。
- 播放/录音初始化前会受控执行 `pactl unload-module module-suspend-on-idle`，避免 USB 耳机或默认声卡在长时间空闲、热插拔恢复或首次切换后出现首段吞音；该动作只收敛在初始化阶段，不在每次提示音、录音或音量键事件里重复切换模块。
- `py_script/usb_audio_mic_test.py --playback` 默认只做“原生采集 + SoX 后处理导出”，不再默认硬套旧 `noise.prof`；若需去噪，先运行 `py_script/usb_audio_noise_profile.py` 生成当前环境底噪 profile，再显式传入 `--denoise --noise-profile <path>`。
- 现场回归优先覆盖两类场景：耳机长时间空闲后的首次播放 `python3 py_script/usb_audio_play_test.py`，以及长时间空闲后的首次录音 `python3 py_script/usb_audio_mic_test.py --playback`；两项测试都应在日志中看到 `Disabled PulseAudio suspend modules: ...`。
- 回滚方式：若需恢复 PulseAudio 默认模块状态，可重启当前用户的 PulseAudio 会话，或重启 `ugripper.service` 让音频守护进程重新按默认环境启动；无需在运行期反复手工切换 `module-suspend-on-idle`。
- 现场 5 步回归 SOP：1）确认耳机已识别且服务正常，观察 `journalctl -u ugripper.service -n 100` 是否出现 USB 音频初始化日志；2）空闲 3~5 分钟后执行 `python3 py_script/usb_audio_play_test.py`，确认首个测试音不吞头；3）再次空闲 3~5 分钟后执行 `python3 py_script/usb_audio_mic_test.py --playback`，确认录音回放起始段不被截断；4）若需覆盖热恢复，再做一次耳机热插拔后重复步骤 2/3；5）若结果异常，记录 `pactl list short modules`、`pactl list short sinks`、`pactl list short sources` 与 `journalctl -u ugripper.service -n 200` 作为现场。

### 8.6 相关辅助单元
- `auto_update/umi-shutdown-trigger.path`：由 `ugripper` 主包交付并启用，监控 `/run/ugripper/system_action_request`；`/run/ugripper` 由随包 tmpfiles.d 配置在开机阶段创建，避免系统镜像中 `/tmp` tmpfs 与 swapfile 链路的 systemd boot transaction ordering cycle 造成 path unit 激活遗漏。
- `auto_update/umi-shutdown-trigger.service`：由 `ugripper` 主包交付，只作为 path 触发的 oneshot helper，不单独 enable；检测到触发文件后执行统一 helper，当前支持 `shutdown` 与 `umount` 两类动作，并把执行结果写回 `/run/ugripper/system_action_result`。
- `auto_calibration/ugripper-network-monitor.service`：仅记录网线状态变化，插拔均不重启数采服务。
- `auto_update/boot_check_install.sh`：开机时检查 `/opt/backup` 中的 `deb` 是否需要恢复或升级。

### 8.7 硬件健康监控
- `record_runtime` 当前参考 v1 口径保留低频硬件健康监控，约每 `1s` 检查一次关键硬件状态，而不是在主循环里做高频主动轮询。
- 当前监控项包括数据盘、主摄、触觉、encoder 与 HMI；默认包还检查 stereo/Fays 节点、进程和 status，`+nostereo` 变体不检查这些双目项。
- 发现磁盘异常时进入 `ERROR_3`；当前会区分 `disk_mount_lost`、`disk_not_writable`、`disk_full` 等 fault key，并按“同类 fault 首次出现打错误日志、持续期间不重复刷屏、恢复时补一条 recovered”收敛日志。发现关键设备节点缺失、HMI 断连或 HMI 长时间无响应时进入 `ERROR_2`，并通过音频守护进程播报 `error`。`hmi_input_inactive` / `hmi_ports_inactive` 的故障证据只包含固定超时阈值和端口路径，不包含持续增长的接收年龄，避免同一故障重复刷日志或反复重置 USB restore 的 `6s` 稳定窗口。`ERROR_2` 会根据缺失路径、stereo 状态或 HMI port 归属到左手、右手、双手或 unknown，用对应侧别灯效提示现场先看哪侧硬件。若 Fays stereo/IMU symlink 在线但单侧 recorder、控制 FIFO 或 START/STOP 控制链路失效，则进入 `ERROR_4`，提示对应侧夹爪控制链路异常，并纳入错误态自动复位策略。绑定 Ego 后，运行时约每 `2s` 探测指定 serial，连续两次失败视为断连并上报 `ego_disconnected / ERROR_5`；录制中会立即按错误停录，空闲时禁止起录，恢复在线后自动清除该连接故障。解绑会停止后续探测并将共享状态固定为 `unbound`，不会把解绑后的物理拔出解释成断连故障。
- 若录制中发现任意关键设备、HMI、数据盘或 stereo daemon 健康故障，当前 episode 会立即按错误停录收尾，写失败 `metadata.json` 和 `validation_error.log`，并把健康监控 fault key 作为显式 `error_type` 进入本条 episode；若停录后又发现文件缺失、finalize 失败等问题，会同时记录内部错误类型，但 `quality_check_err_type` 只保留最高优先级主类型。设备后续恢复只影响下一次录制，不会把本条数据恢复成成功。
- 若异常恢复发生在空闲态：回到 `READY` 并补播 `ready`。
- 自动复位策略当前只覆盖可能由夹爪侧 USB/供电重枚举恢复的错误：`ERROR_2`（关键硬件/HMI/stereo 缺失或不活跃，包含主摄 V4L2/USB 启动失败、主摄明显短流和 CameraRecorder D 状态卡死）、`ERROR_4`（stereo/Fays 控制链路失效）和主摄相关 `ERROR_1`（错误文本指向 `main camera`、`cam_left/right/chest` 或 `/dev/cam_*`）。`ERROR_3` 是数据盘挂载/可写/空间问题，不触发复位；`ERROR_5` 是 `runtime_error` 或其他运行时异常兜底，也不默认复位。若某侧夹爪完全未枚举（该侧 HMI 和关键传感器 symlink 都不存在），运行时只保留错误提示，不触发软件复位且不消耗复位次数；检测到该侧任一关键节点或 HMI 重新出现时，会重置当前复位次数，并给该侧约 `20s` 插入稳定窗口，窗口内健康监控错误、Fays ready 未完成或主摄缓存未读完都不会触发软件复位。symlink/硬件健康缺失类触发进入自动复位前会先进入约 `6s` 自恢复缓冲窗口，要求同一 `cause/evidence/side` 证据持续存在才执行复位，避免 symlink/udev 瞬时抖动或可自恢复重枚举直接触发 USB 断电；窗口开始时日志输出 `restore usb pending wait cause=... evidence=... side=... stable_ms=6000`，窗口内恢复会输出 `restore usb pending cleared ...` 并取消复位。主摄 V4L2/USB 启动失败、主摄明显短流、CameraRecorder D 状态卡死和主摄校验类一次性证据会直接触发，不进入稳定等待。确认触发时日志固定输出 `restore usb requested cause=... evidence=... side=...`，其中 `evidence` 直接给出缺失节点或关键错误证据。触发命令为 `sudo -n /usr/local/sbin/ugripper_restore_usb`；真正执行断电复位前，若仍在录制会先按错误停录完成 episode 收尾，停录返回失败但录制状态已经结束时仍进入自恢复缓冲窗口；只有 D 状态卡死这类 CameraRecorder 内核等待故障会在录制无法停住时继续进入复位流程，用于释放 V4L2/USB 内核等待。随后同步运行日志并通过 `/run/ugripper/system_action_request=umount` 请求 root helper 卸载 `/mnt/data_disk`，数据盘未挂载时直接继续，卸载失败时跳过本次复位且不消耗复位次数；只有 D 状态卡死允许在卸载失败时继续复位。随后运行时会先暂停 Fays stereo daemon/recorder，并在等待窗口内阻止 supervisor 自动重启 Fays，避免 SDK 在 USB 断电重枚举期间占用 stereo/IMU 节点。每次复位完成后会按约 `1s` 周期检查全部当前启用的关键传感器 symlink；节点齐全后立即恢复 Fays daemon，并从该时间点最多继续等待约 `45s`，要求健康监控恢复且主摄相关触发时主摄 SN/标定缓存恢复；等待 symlink 阶段只在缺失列表变化或约 `5s` 间隔输出 `restore usb symlinks waiting attempt=... evidence=...`，若复位完成后约 `25s` 仍缺 symlink，或 symlink 齐后约 `45s` 仍未健康恢复，则进入下一次复位。同一错误窗口最多连续复位 `3` 次，第三次仍在 symlink 或 ready 阶段超时后，会沿用 HMI 蜂鸣器链路对故障侧夹爪报警，未知侧或双侧故障时两侧同时报警，单次蜂鸣最长 `5s` 后自动关闭；关闭命令会结合状态回读确认，状态仍非 `0/0` 时最多重试 `5` 次。等待窗口内如果健康监控提前恢复并回到空闲态，会立即认定本轮复位成功并清除等待窗口，后续再异常按新的故障窗口处理；重新开始录制也会清除本轮复位窗口并关闭蜂鸣。等待窗口内的重复硬件健康报错不会抢跑触发下一次复位。该 root wrapper 会在存在 `bluetooth_gatt.service` 时先停止服务以释放通信接口，不存在或 stop/start 失败只记录告警，不阻断 GPIO/电源板复位主流程。供电板寄存器 `0x10` 当前按低 4 bit 逐位写入：下电逐位清零，上电逐位置位并保留短间隔错峰上电；每个 bit 写入后读回校验，整组完成后再次校验，失败时有限重试。
- `camera_recorder` 仍保持“单路 recorder 失败不立即主动终止整次录制”的容错语义；本次实现只加强停录阶段的子进程组回收与 stop 日志，不把启动期短暂抖动直接升级为全量停录。

## 9. 配置、安装与 U 盘流程
### 9.1 当前主要配置入口
当前主要配置来自 `/etc/environment`。

| 键 | 当前用途 | 备注 |
| --- | --- | --- |
| `DEVICE_SN` | 决定数据路径与标定导入匹配目录 | 建议视为必填 |
| `UGRIPPER_LANG` | 提示音语言 | 由 `config.txt` 导入 |
| `CAMERA_CODEC` | `camera_recorder` 启动参数，并同步 ego 视频编码 | 仅支持 `h264` / `h265`；ego 映射为 `avc` / `hevc` |
| `ENABLE_CHEST_CAM_MAIN` | `record_runtime` 读取 | 默认启用胸部主摄；显式写成 `0/false/no/off/disable/disabled` 时关闭 |
| `UGRIPPER_EGO_SERIAL` | `ego_recording_worker.py` 读取 | 默认不指定 serial，自动扫描 `adb devices`；需要固定某台 ego 时可设置 |
| `UGRIPPER_EGO_ADB` | `ego_recording_worker.py` 读取 | 覆盖 ADB 可执行文件路径；默认优先使用随包安装的 `bin/UgripperRuntime/adb/adb`，不可用时回退 `PATH` |
| `UGRIPPER_EGO_SYNC_INTERVAL_SEC` | `ego_recording_worker.py` 读取 | ego 文件增量同步间隔，默认 `1s` |

说明：双目只允许在构建时通过 `UGRIPPER_ENABLE_STEREO=OFF` 关闭，不提供环境变量文件、`config.txt` 或运行时命令配置。默认构建为 `ON`。

### 9.2 当前默认项
| 项目 | 当前默认口径 |
| --- | --- |
| 部署形态 | 单机双手、本地录制 |
| 录制相机集合 | 默认左右主摄 + 左右 stereo + 4 路触觉；`+nostereo` 变体去掉 stereo/Fays；胸部主摄仍由 `ENABLE_CHEST_CAM_MAIN` 控制 |
| 网络 | 业务可在无对端设备时启动 |
| 静态 IP | 主包不托管，沿用系统现有有线配置 |

### 9.3 U 盘支持内容
- 自动安装名单：
  - `das-usb-updater`
  - `ugripper`
  - `bluetooth-gatt-server`
  - `databot-device-joint`
  - `device-ota-mender`
- updater 自升级：根目录放置 `das-usb-updater*.deb`；若本次先装的是新版 updater，安装后的新脚本会在同一次插盘流程里继续按最新名单扫描剩余 `.deb`。
- 主包升级：根目录放置 `ugripper_*_arm64*.deb`。
- 配置导入：根目录 `config.txt`。
- 标定数据导入：`ugripper_calib/<DEVICE_SN>/`，通过文件名后缀 `_left` / `_right` 区分左右主相机 `camchain`；对应夹爪侧 RGB/stereo/IMU payload 则按现场读出的 gripper SN 文本在该目录下递归匹配，优先 `.bin`，其次包含 `summary/imucam` 关键词的 `.md` 或当前 raw 目录（`rgb_video_ros-camchain.yaml + output-results-imucam.txt`），并按 `rgb_video_ros_imucam_parameter_summary.md` 口径生成 `1024-byte` 对齐数据结构；当前固件写入时固定补满 `64 x 16B` 传输窗口。
- encoder 零位校准触发：根目录 `calibration.txt`。
- deb 安装灯效：当本次 U 盘里实际存在目标软件包并进入安装窗口后，`usb_auto_update.sh` 会停止业务栈并通过 HMI helper 显示 `CALIB_RUN` 黄灯快闪；安装中灯效至少保持 `3s`，避免短安装或主包资源替换导致肉眼不可见；任一包安装或安装后置处理失败时显示 `ERROR_1` 红灯 `3s` 并退出；全部目标包处理成功后显示 `CALIB_DONE` 绿灯 `3s`，随后先恢复业务服务，确认服务恢复后再继续升级完成提示音。
- 安装完成提示音：当本次 U 盘里实际出现的目标软件包全部安装/重装完成后，`usb_auto_update.sh` 会直接从新安装的 `/opt/ugripper/audio*/upgrade_completed.wav` 里选取对应语言资源，并以 `paplay` + PulseAudio 播放升级完成提示音；当前不要求自动安装名单里的包必须全部同时出现在 U 盘。若现场没有可用 PulseAudio sink，则只记日志，不把安装流程判失败。

### 9.4 U 盘同次插入顺序
当同一次 U 盘插入同时包含 `config.txt`、`ugripper_calib/` 和 `calibration.txt` 时，当前顺序是：
1. 导入 `config.txt`
2. 按 `_left` / `_right` 文件后缀识别需要导入的左右标定目标，并逐侧读取现场 gripper SN、匹配对应夹爪标定 payload
3. 只有当本次目标侧都完成 gripper SN 匹配后，才开始写入对应夹爪的整套 RGB/stereo/IMU 标定；任一侧匹配失败或写入失败时，本次不会把旧 persist calibration 当作成功结果继续保留
4. U 盘导入当前负责“匹配并写入夹爪标定 + 保存导入归档”；主机侧持久化 `calibration.json` 由 `record_runtime` 在 gripper 后续插入/重连时按实际读回的 SN 与 calibration payload 刷新
5. 若存在 `calibration.txt`，则跳过中间重启，直接进入左右编码器并行校准流程
6. 若未触发 `calibration.txt`，则开始按固定名单依次处理 `das-usb-updater`、`ugripper`、`bluetooth-gatt-server`、`databot-device-joint`、`device-ota-mender` 的 `.deb`；每个包都会先按 Debian 包名读取版本，U 盘内若存在多个候选文件则取最高版本；只要名单内包在 U 盘根目录存在，就执行安装，同版本也会强制重装；进入 deb 安装窗口后显示 `CALIB_RUN` 黄灯快闪且至少保持 `3s`，任一包失败显示 `ERROR_1` 红灯 `3s` 并退出
7. 若 updater 在第 6 步先完成自升级，则安装后的新脚本会在同一次插盘流程里继续执行剩余自动安装名单，避免必须二次插盘才能让新名单生效
8. 全部目标软件包安装完成后，先显示 `CALIB_DONE` 绿灯 `3s`
9. 恢复 `ugripper.service` 并确认业务服务可启动；若恢复失败，本次升级按失败处理，不播放完成提示音
10. 若新主包已提供 `upgrade_completed.wav` 且现场存在可用 PulseAudio sink，则播放升级完成提示音

### 9.5 安装脚本与网络行为
`pack_script/postinst` 当前会：
- 停掉旧的录制相关进程。
- 初始化持久化标定目录；若 `calibration.json` 缺失、空文件或非法 JSON，则自动用 `bin/UgripperRuntime/config/fakeCamCalib.json` 修复。
- 自动配置 exfat 提前加载：若现场仍使用 `/home/user/lib/exfat.ko` 外部模块，安装时会把它复制进 `/lib/modules/<kernel>/extra/`、写入 `/etc/modules-load.d/ugripper-exfat.conf`，并在本次安装窗口内尝试立即加载。
- 在重放相机/音频/串口 udev 规则前再次停止 `ugripper.service` 并等待停稳，避免 Fays warmup daemon 持有 stereo/IMU video 节点时被 trigger 扰动。
- 重新加载 udev 规则，并只重放 Fays USB、video4linux、sound、input 与已枚举 CH9344 tty 事件；不主动触发 block add，避免升级期间重复拉起 USB updater。
- 创建 `/run/ugripper` 运行时目录，清理旧 `/tmp/umi_system_action_*` 与当前 `/run/ugripper/system_action_*` 残留请求，随后启用并启动 `umi-shutdown-trigger.path`；该 path/service 归 `ugripper` 主包唯一交付，`das-usb-updater` 不再打包同名单元，避免 `/lib/systemd/system` 与 `/etc/systemd/system` 的同名覆盖造成系统动作请求无人消费。
- udev trigger 完成后手动 `start ugripper.service`，并等待服务进入 active；若启动失败，主包 postinst 直接失败，由 U 盘升级流程按安装失败处理。
- 启用并重启 `ugripper-network-monitor.service`。

网络相关当前行为：
- 主包不再创建、更新或删除有线 NetworkManager 连接。
- 主包安装/卸载默认沿用系统原有的有线 IP 配置，不对现场网络拓扑做接管。
- 当前录制启动不依赖对端网络存在。

数据盘挂载当前行为：
- `config/99-fixed-usb-map.rules` 会在允许的物理 USB 口上调用 `mount_data_disk.sh`，固定挂载点仍然是 `/mnt/data_disk`。
- 主包规则现在只负责挂载/卸载；若额外安装了可选包 `das-usb-updater`，则由 updater 包自己的 `99-usb-auto-update.rules` 通过 `SYSTEMD_WANTS` 拉起 `usb-auto-update@<dev>.service`。
- 允许的 USB 分区在 `remove` 事件里会显式对 `/mnt/data_disk` 执行卸载清理；此外还保留了 USB block `remove` 的兜底触发，尽量覆盖 hub 断链或热插拔时分区级事件不完整的场景，避免拔盘后残留 stale mount。
- `mount_data_disk.sh` 当前除了匹配挂载源设备节点，还会把“挂载点只读”“挂载源设备节点已不存在”或“挂载点已不可访问”视为脏状态并优先清理；清理同一数据盘时会同步卸载同源的其他挂载点（例如桌面 automount 到 `/media/ubuntu/...` 后又挂到 `/mnt/data_disk` 的双挂载场景），避免复位前只卸载业务挂载点却仍有同一 USB 盘保持 mounted；它不再主动启动 updater service；`run_record.sh` 也会把这类状态视为未就绪。

### 9.6 网线监测行为
`auto_calibration/monitor_network.sh` 当前行为：
- 网线拔出：仅记录状态，不重启 `ugripper.service`
- 网线插入：不重启 `ugripper.service`，也不执行其他额外动作
- 若存在 `/run/ugripper_installing_from_usb.lock`，则跳过升级窗口内的边沿动作

### 9.7 主包打包约束
- 主包当前仍直接携带项目内 `.venv` 与 `.venv/.python-runtime`，部署后继续以 `/opt/ugripper/.venv/bin/python3` 作为首选解释器入口。
- `build_deb.sh` 的 staging 目录默认按增量方式复用：项目主体与 `.venv` 分开同步，避免每次打包都先删除再完整重拷 `.venv`。
- `build_deb.sh` 默认 `dpkg-deb` 压缩口径为 `xz -1`，兼顾构建速度与包体积；`build_deb.sh -q` 仍跳过 C++ 编译，并沿用同一默认压缩口径。如需在速度与包体积之间切换，可通过 `DPKG_DEB_COMPRESSOR`、`DPKG_DEB_LEVEL`、`DPKG_DEB_STRATEGY`、`DPKG_DEB_UNIFORM_COMPRESSION` 覆盖默认参数。
- 主包打包当前默认优先从 Nexus raw 仓库下载并解压归档好的 `.venv` `tar.gz`，再沿用同一套 `.venv` 架构校验与 staging 同步逻辑；当前默认归档为 `ugripper-v2-uv-venv/py311_v2.1.0/ugripper_venv_20260623_102954_arm64.tar.gz`，这条入口只针对 raw 制品下载，不走 Conan recipe。
- 若需要临时切回本地或外部目录中的 `.venv` / `build`，可显式设置 `PACKAGED_VENV_URL=''` 后再配合 `PACKAGED_VENV_SOURCE`、`PACKAGED_BUILD_DIR` 覆盖来源；若最终 `.venv` 来源不存在，脚本会同步移除 staging 中旧的 `.venv`，此时包仍可生成，但不再满足部署后直接运行的交付约束。
- auto-release 源码基线独立保存在 `temp_build_state/`，不进入主包或 updater staging。候选文件由 Git 已跟踪文件和未忽略的新文件产生，排除文档、Codex、graphify 与构建产物；哈希采用 NUL 分隔批处理和原子替换。主包/updater 构建后的基线刷新默认限时 `30s`，失败或超时时保留上一份基线并告警，不影响已生成的 deb。

## Web 触觉盖板诊断与 Ego 版本

- Ego 绑定读取固件 `ro.internal.display.id` 与 App `versionName`，在线 probe 每分钟补读/更新；页面显示两项版本，断连标注缓存。
- 触觉图片独立按夹爪侧别与传感器位置展示；“盖板损坏检测”按实时采集参数取图，对照同 serial 的短期 reference、长期 persistent 输出 mask 状态图，不直接更新连续 episode 黄灯窗口。
- 允许确认当前盖板完好、无接触后按传感器重置指定基线。主运行时消费 `tactile_state/web_reset_request.json`，等后台校验结束后串行更新 baseline/history/黄灯，并回复 `web_reset_response.json`；请求超时、serial 变化均拒绝。不重启双目预热。
- Web 临时预览仅保留最新一组，过程视频与重置中间文件清理，服务启动清理中断残留；正式基线和固定正常/损坏例图保留。

## 手动相机抓图

- Web 设备概览与 `/usr/local/bin/getimg [输出目录]` 支持按采集配置抓图，录制/维护期间拒绝执行，共用现有 maintenance flock，不停止主服务或双目 warmup。
- 双目由 Fays recorder 的 `GETIMG|<请求ID>` 从原 SDK 取流线程复制下一帧，控制线程保存；其他本机相机直接采集，沿用 camera YAML 的分辨率、帧率、输入格式、输出变换及环境中的主摄编码、胸部开关、电源频率。
- Ego 使用独立按钮或 `getimg --ego`，短录约 3 秒后拉取三路视频截帧；全部成功后仅删除本次新建的远端测试 episode，失败保留并报告。详见 `standalone/UgripperWebServer/README.md`。

## 10. 常用检查与排障入口
### 10.1 服务管理
```bash
sudo systemctl status ugripper.service
sudo systemctl restart ugripper.service
sudo journalctl -u ugripper.service -f
```

### 10.2 录制相关进程
- 主运行时：`record_runtime`
- 相机录制：`camera_recorder`
- 传感器录制：`sensor_recorder`
- 音频播放：`audio/audio_play.py`

### 10.3 U 盘流程日志
```bash
sudo tail -n 200 /var/log/ugripper/usb_auto_update.log
sudo tail -n 200 /var/log/ugripper/boot_install.log
```

### 10.4 运行日志

测试工程师可复制 `tools/log_viewer/` 目录并用 Chrome / Edge 打开 `index.html`，一次离线打开一份运行日志，新文件替换当前内容，按等级、错误码、时间、关键词筛选，查看简短中文故障提示、restore 请求/执行/恢复事件计数及上下文。无需安装运行环境；详细统计口径见 `tools/log_viewer/README.md`。

```bash
tail -n 200 /tmp/umi_sys_<device_sn_lower>_$(date +%Y%m%d).log
tail -n 200 /mnt/data_disk/logs/umi_sys_<device_sn_lower>_$(date +%Y%m%d).log
```

### 10.5 设备映射优先检查
优先检查以下 symlink / 设备是否存在且方向正确：
- `/dev/cam_left`
- `/dev/cam_right`
- `/dev/cam_chest`（启用胸部主摄时）
- `/dev/tcam_left_l`
- `/dev/tcam_left_r`
- `/dev/tcam_right_l`
- `/dev/tcam_right_r`
- `/dev/left_encoder`
- `/dev/right_encoder`
- `/dev/left_gripper`
- `/dev/right_gripper`

### 10.6 快速定位建议
- 不能启动：先看 `ugripper.service` 日志和 `record_runtime` 是否成功拉起。
- 能录不能停：优先检查 HMI 按键事件、状态机和 `sensor_recorder` / `camera_recorder` 退出路径。
- 少文件或校验失败：先核对默认八路视频、胸部主摄启用时的第九路视频、双 MCAP、`metadata.json`、`calibration.json` 是否完整；若已进入 `ERROR_1`，优先查看 episode 下的 `validation_error.log`；若进入 `ERROR_3`，优先检查 `journalctl -u ugripper.service` 中的 `disk_mount_lost / disk_not_writable / disk_full` 日志。
- warmup 一起双目就掉线：先查是否同时存在多份 `camera_recorder --stereo-daemon`。重复 warmup daemon 抢占同一批双目视频设备时，可能把双目打进 `recovering`，严重时会伴随 USB 侧重枚举；先清掉多余 daemon，再观察 `/tmp/umi_stereo_camera_status.json` 与 `journalctl -u ugripper.service -n 200`。
- tactile serial 不对：先看 `journalctl -u ugripper.service` 中 `tactile camera runtime serial cache` 相关日志，确认 `/dev/tcam_*` 插入、symlink target 变化和 sysfs serial 缓存刷新结果；再只读核对 `/sys/class/video4linux/<videoN>/device` 向上 USB 父节点的 `serial`，并对比 episode `calibration.json` 中 `observation.images.tcam_left_l.serial`、`tcam_left_r.serial`、`tcam_right_l.serial`、`tcam_right_r.serial`；当前口径只修正 episode，不回写 `/etc/ugripper/config/calibration/calibration.json`，且不再输出旧 tactile 键。
- 进入错误灯效但录制进程还活着：优先检查 `/mnt/data_disk` 是否仍可写、关键 `/dev/*` 设备节点是否还在，以及 HMI 是否持续响应。
- 音频异常：优先看 USB 耳机枚举、PulseAudio sink/source、`module-suspend-on-idle` 是否已在初始化阶段被卸载。
- 运行日志缺失：先看 `/tmp/umi_sys_<sn>_<date>.log` 是否生成，再看 `/mnt/data_disk/logs/` 是否存在当天镜像，最后核对 `/mnt/data_disk` 是否仍是真实可写挂载点。
- U 盘流程异常：先看 `/var/log/ugripper/usb_auto_update.log`，再区分是包安装、配置导入、标定导入还是 encoder 校准失败。

### 10.7 tactile serial 定向核对
```bash
journalctl -u ugripper.service --since "2 hours ago" | grep 'tactile camera runtime serial cache'

for dev in /dev/tcam_left_l /dev/tcam_left_r /dev/tcam_right_l /dev/tcam_right_r; do
  node="$(basename "$(readlink -f "$dev")")"
  echo "===== $dev -> $node ====="
  p="$(readlink -f "/sys/class/video4linux/$node/device")"
  while [ "$p" != "/" ] && [ -n "$p" ]; do
    if [ -f "$p/serial" ]; then
      echo "$p/serial: $(cat "$p/serial")"
      break
    fi
    p="$(dirname "$p")"
  done
done

python3 - <<'PY'
import json, pathlib
path = pathlib.Path('/mnt/data_disk/<device_sn_lower>/data/episode_<date>_<index>/calibration.json')
data = json.loads(path.read_text())
for key in [
    'observation.images.tcam_left_l',
    'observation.images.tcam_left_r',
    'observation.images.tcam_right_l',
    'observation.images.tcam_right_r',
]:
    print(key, data[key]['serial'])
PY
```

### 10.8 HMI 版本读取
- `GripperHmiTool --port /dev/<side>_gripper --read-version` **禁止发送**版本初始化/写入帧 `5A 5A 57 56 65 72 16`；现场已确认该帧会清空或覆盖 HMI SN，因此版本读取只允许发送只读命令。
- Tool 直接依次发送只读帧 `5A 5A 52 56 00 00 04` 与 `5A 5A 52 57 00 00 05`，两路读取互相独立；某一路返回 `F2 function_error` 时仍继续读取另一路。
- CLI 稳定输出分别为 `version_56=<value|UNAVAILABLE>` 与 `version_57=<value|UNAVAILABLE>`；至少一路成功时命令成功，两路均失败时返回非零。固件当前观察到的响应为无帧头 16-byte ASCII，也兼容带 `A5 A5` 头和 XOR 的 19-byte 响应。
- 生产强检不预设固定来源，而是从两路成功结果中选择命中允许表的版本；例如 `52/56=V1.2.1`、`52/57=V1.0.02.20260324` 时选择后者。

### 10.9 仓库内测试脚本
- 仓库根目录下的 `test/scripts/` 用于存放仓库侧、现场定向验证用脚本，不属于 `ugripper.service` 默认运行链路。
- 当前目录包含：
  - `camera_test.sh`：直接拉起多路 `ffmpeg` 验证非主相机录制稳定性，并记录运行态信息。
  - `camera_crash_capture.sh`：在 `camera_test.sh` 基础上额外持续抓取内核日志、进程、中断与内存信息，适合定位 crash / hang。
  - `testVideoPipe.sh`：快速枚举指定 `/dev/video*` 节点的视频格式能力。
  - `scan_main_camera_mkv_issues.py`：递归扫描 `cam_left.mkv` / `cam_right.mkv`，并发检查主摄 `mkv` 的包时间戳异常、显著时间洞与可疑解码错误，并对齐同目录左右主摄的异常时间点。
  - `validate_episode_data.sh`：测试工程师离线 episode 校验入口，自动调用深度校验器检查文件、metadata、视频/MCAP gap，并优先输出全部视频、全部已发现 sensor topic、左右 Fays IMU/camera 和可选 Ego sensor topic 的 Unix 首帧时间、换算方式及相对误差；Ego 单调时钟 topic 按 `head_pose_details.start_offset_us` 固定 offset 对齐，全流最大首帧误差达到 `1000ms` 时失败。
  - `board_gripper_hmi_link_stress.sh`：停止主服务后反复独占连接左右 HMI，验证状态回复、RGB 和蜂鸣器开关回读，并并行运行 `SensorRecorder` 检查同颗 CH9344 上的 encoder UART 是否持续产出；默认将 encoder 最大相邻时间戳 gap 限制为 `100ms`，避免平均频率掩盖秒级数据洞；结果默认写入 `/dev/shm`，便于在加密环境中中转部署和回收。
  - `board_ch9344_interference_matrix.sh`：按空载、原始 open/close、termios 配置、`TCIOFLUSH`、`TIOCEXCL`、原始状态/RGB/蜂鸣器帧、完整 HMI 重连和长连接分阶段施加干扰，每阶段独立记录左右 encoder MCAP 与 gap 事件，用于定位 CH9344 多 UART 相互影响的具体触发操作。
  - `board_encoder_to_hmi_interference_matrix.sh`：反向保持左右 HMI 长连接和状态查询，同时对指定 encoder UART 高频执行 open/close、配置、flush 与真实 1Mbps 请求，统计同侧/异侧 HMI 回复年龄和 inactive 样本，确认 encoder 操作是否会触发 HMI 无响应。
- 安装后的 `bin/CameraRecorder/main_camera_xu_tool` 支持 `--rtc-get`、`--rtc-set-us`、`--rtc-sync-now` 与 `--parse-h265-timestamps`，用于独立验证主摄 XU RTC 与 H.265 XU2 SEI；`--rtc-get` 输出固件在最近一次 SET 时锁存的 encoder `Pset`，`--rtc-sync-now` 使用与录制链相同的 `+200us` 写入 `Tset` 并输出 `Pset`，逐帧时间按 `Tset + Pframe - Pset` 换算。
- 建议从仓库根目录显式执行 `bash test/scripts/<script>.sh`；详细参数与注意事项见 `test/README.md`。
- 单条 episode 可执行 `bash test/scripts/validate_episode_data.sh <episode_dir>`；父目录下校验最新一条可追加 `--latest`，机器消费结果可追加 `--json`。
- `py_script/read_ugripper_mcap.txt` 是给数据使用者参考的通用 MCAP 读取示例；文件后缀使用 `.txt`，方便在会拦截或加密 `.py` 的环境中发送，但内容仍是 Python 代码。它可直接输入单个 `.mcap` 或完整 episode 目录；episode 模式会自动读取 `sensor_left/right.mcap`、`fays_data_left/right.mcap` 与可选 `ego/sensor.mcap`，并解码当前 UGripper encoder 与 Fays `i/c` 二进制 payload。配套说明见 `py_script/README_read_ugripper_mcap.txt`。示例：
  `python3 py_script/read_ugripper_mcap.txt /mnt/data_disk/<device_sn_lower>/data/episode_<date>_<index> --json-out /tmp/ugripper_mcap_report.json`。
- 当前 UGripper MCAP 是通用 MCAP 容器，不是 ROS 2 bag；不要用 `ros2 bag info` 或 Foxglove 的 ROS bag 可视化入口作为数据是否正常的判断依据。若要可视化，需要按脚本里的 payload 定义转换成 Foxglove/ROS 可识别 schema 后再导入。
- 若现场需要脱离主服务、单独向夹爪 HMI 写入 SN / 标定，可使用 `py_script/hmi_sn_batch_writer.py`：脚本直接读取一个或多个 `.xlsx` 的多 sheet `SN码` 列，支持操作员输入 SN 后 4 位或任意连续片段做唯一匹配，自动扫描 CH9344 相关串口确认可用 HMI 口，并在匹配到目标 SN 后写入 SN 与可选 `1024-byte` 标定 payload。
- `py_script/hmi_sn_batch_writer.py` 默认会在启动时把自身和本次使用的 `.xlsx` 同步复制到 `/mnt/data_disk/hmi_sn_writer/`；标定源目录可通过 `--calib-root` 指向包含 `.bin`、summary `.md` 或 `rgb_video_ros-camchain.yaml + output-results-imucam.txt` 的目录。若未找到目标 SN 对应标定文件，脚本会只写 SN，并在控制台用黄色提示“未写标定”。

## 11. 当前使用注意点
- `config/camera_recorder.yaml` 保留完整相机配置；默认包启用 Fays 双目，`+nostereo` 变体不启动。胸部主摄仍由 `ENABLE_CHEST_CAM_MAIN` 决定。
- 仓库内仍有部分后处理脚本依赖旧输出命名或旧假设，不能默认视为当前主链路的一部分。
- `test/scripts/` 下的脚本属于仓库侧辅助工具，默认不随主包安装；若现场需要长期保留，应明确同步部署方式与使用说明。
- `info.json` 不再作为 episode 最终产物生成；时间偏移信息已迁移到 `metadata.json.video_details[].start_offset_us`。
- 现场调试若需要同时访问不同子网设备，应由系统侧或人工维护额外地址/路由；主包不负责托管这些网络策略。
- 校准、导入与恢复动作仍分散在多个 shell 脚本和 service 中；当前功能可用，但维护时需要注意入口分散。

## Web 生产压测、编码器与界面语言

- Web 默认跟随 `UGRIPPER_LANG`；右上角临时切换中英文，不修改设备配置，刷新后恢复默认。
- 配置页编码器读取使用持续串口连接，目标 100 Hz，NDJSON 推送；显示实测频率，浏览器按显示刷新节拍绘制。关闭/离开页面主动取消，断连写超时释放，每次约 60 秒硬截止兜底，恢复原串口参数。会话期间与录制、维护互斥。
- 生产压测由独立维护进程执行：A 为 (120 秒录 + 20 秒停) ×51，B 为 (300 秒录 + 20 秒停) ×11，依次执行，包含最后停止间隔。启动/校验额外耗时计入剩余时间估计。
- A 失败 ≥3、B 失败 ≥2 或总失败 >3 即不通过；达到阈值继续完整计划。按正式 metadata 质量结论、录制时长、必需文件与 validation_error.log 判定每轮，启动失败及提前停止也计失败。
- `/run/ugripper/production.lock` 独占整个压测，Web 其他操作共享检查此锁；录制沿用 maintenance.lock，不受压测自身锁阻挡。压测持有 USB 升级锁。不要在压测期间使用实体按钮或其他入口控制录制。
- `/var/lib/ugripper-web/production.json` 保存最新结果，production-results 保留最近 20 次报告。网页关闭或断网不取消，手动取消等待自身录制收尾；维护服务重启将中断任务标记为 interrupted，不自动续跑、不判通过。正式录制数据不自动删除。

Web 右上角语言选择器固定显示 `Language / 语言` 标签；录制数据列表在分页前按名称倒序排列，最新日期/编号的 episode 优先展示。

### 主摄 50 Hz 插补时间修正

`main_camera_cadence.h` 对 H.264/H.265 共用编码 PTS 检测：以 20 ms 为参考周期，采用宽容差及长/短/恢复间隔的相对比例联合识别，避免固定窄毫秒门限或按帧号机械判定；仍排除正常等间隔、回退和明显丢帧。仅在成功配置 powerline=50 Hz 且输出 60 fps 时启用。CaptureLoop 复制码流并归还 V4L2 buffer 后，用最多两帧 lookahead 确认；仅将插补帧和随后两真实帧的编码器时间设为前一真实帧编码器时间 +1/+20000/+40000 μs，然后各帧分别使用自身 SEI 对应的 Tset/Pset 映射 Unix 时间，保留窗口内 10 秒校时产生的共模偏移。其他帧维持原始映射；已有单调性保护仍保留，负向校时跳变造成回退时可能触发最小钳制。未知间隔不压缩，原始 SEI 不修改，修正写入容器 PTS/DTS，并记录 main_50hz_duplicate。停止时先给予 capture worker 250 ms 排空窗口，再沿用 EOS/flush 有界停止；无法确认的尾帧使用原始时间。主摄 metadata 的绝对起点净补偿为 -28000 μs：用户测得的 +36 ms 已包含原 +8000 μs，因此按 +8000−36000 得到净值。后两帧的短/恢复间隔允许互换，以覆盖编码阻塞导致的延迟重新分配。固件变化后需重新验证模式及曝光延迟。

局部特征使用原始 encoder PTS，历史稳定条件为前三间隔 20±3 ms；不使用修正后的间隔反向确认。H.265 还结合相对 VCL 大小识别 I 帧附近重复帧，该大小仅为辅助证据，不是固件重复标志。

### H.265 插补相位跟踪

`main_camera_h265_continuity.h` 解析单层、无 B 帧的 SPS/PPS/首个 slice POC，结合 V4L2 sequence 检测编码图像连续性；未知或不连续时解除锁定。锁定后以六帧周期预测，不再逐组要求前三间隔稳定；邻近 ±1 帧强特征允许重新定相位，仍检查正间隔和窗口跨度。I 帧附近结合相对 VCL 大小处理重复候选。跨 IDR 仅有 V4L2 连续性证据，不能证明传感器编码前未漏采。

0008 的独立 LED 有效标定区间左右各 980/980 命中、无额外预测，不覆盖未标定开头和任意场景。0012 的 100 秒 H.265 验证左右修正 998/997 组，各 10 个校时锚点，所有容器 PTS 与预期映射一致且解码正常；这是实现验证，不等同于光学同步精度结论。H.264 保留局部 PTS 识别入口但不使用 H.265 跟踪；0011 原始 encoder 间隔固定 16.666 ms，未命中重复修正规则，独立适配和曝光延迟验证暂缓。

### H.264 新固件适配（2026-09-16）

`2026-09-16-2127` 恢复带长短间隔的原始 H.264 encoder PTS。主摄使用 `main_camera_h264_continuity.h` 解析渐进式 reference I/P 的参数集和 frame_num，结合 V4L2 sequence 作为连续性证据，复用 H.265 的局部间隔、VCL 大小、六帧周期辅助及仅重复帧+随后两帧的修正。未知/截断、B帧、非参考帧、隔行、slice groups等不支持模式保守拒绝；frame_num跳号或传输序号不连续解除锁定。IDR边界仅有传输连续性证据，不能证明编码前未丢曝光。旧1728固件固定16.666ms的H.264不满足局部捕获特征，不应据此宣布适配旧固件。XU每10秒校时、逐帧锚点和净-28ms绝对补偿保持；新固件H.264实际曝光偏移需要独立毫秒表标定。

I 帧后重复帧识别补充：当帧序号连续、已锁定周期且完整长短间隔特征成立时，候选不再必须满足 VCL 大小比例。允许预测位置附近一帧重定相位，短间隔可位于后两帧任一处；未知相位、断号和正常等间隔不因此获得修正。0014 已标定范围左右各 982/982 命中，0008 H.265 离线结果保持不变，不能扩展为任意场景全量保证。
