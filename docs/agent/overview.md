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

### Fays IMU 回调、授时与 CPU 分工

- Fays IMU 使用 SDK 的单生产者回调接收原始样本，回调仅复制数据、记录接收时刻、入预分配的 8192 槽队列并唤醒消费者；不调用 latest-value 轮询接口，不在 SDK 回调中写 MCAP、打印或等待业务锁。消费者持续维护映射并提交有界 MCAP 队列。队列满或录制中的设备时钟回退会记录 `UGR-22-06` 并退出 recorder，由现有 stereo 控制链路故障处理停录；不能将数据丢失静默视为成功。
- `device→steady` 和 `steady→Unix` 偏移分别按 100ms 窗口的最小接收偏移更新。冷启动至少积累 200ms；单次迟到不直接改映射，超过 2ms 的偏差需要 5 个连续窗口、窗口间偏移相差不超过 0.5ms 才进入更新。仅录制期间每个偏移轨道按误差的 10% 更新且最多 1ms/s；待机及 STOP 排空完成后，小偏差按误差的 10% 平滑逼近，已确认的大偏差直接重建，通常约 500–600ms 收敛，不再对秒级 NTP 校时执行数小时慢追。直接观察 `Unix-steady` 变化，因此 NTP、人工校时等都会生效，无需依赖某个 NTP 进程通知。该方法只能抑制接收抖动，无法从单向观测严格区分持续传输延迟与真实时钟偏移。
- 连续 1s 无样本时映射暂不可用；设备时钟继续前进则保留原映射，恢复收样并重新确认，不能用恢复后的第一条迟到样本跳变重置。设备时间回退才冷启动重建。待机大偏差待确认期间 mapping ready=false；单样本偏差达到 50ms 时立即阻止起录，经窗口过滤确认或恢复后再放行。普通 episode 切换沿用已收敛的待机映射；冷启动/恢复时 START 最多等待 1s，仍不可用则拒绝本次 START，后续可重试。录制中单次系统时间抖动不触发跳变重建或丢帧；系统时间偏移达到 50ms 且经 5 窗口确认后报 `UGR-22-06` 并退出 recorder，由已有链路故障处理停录，避免混合时间基准的数据被视为成功。相机发布时刻仅在同一录制会话内保护严格递增，待机/旧会话未来时间不能阻挡新会话向后对齐；真实设备 gap 不作插值修补。IMU 原始时间及其接收时刻的 MCAP 语义保持不变。
- START 同时设定会话 ID 和基于当前映射的设备时间下界，回调接收时会话不匹配、时间早于下界的积压样本均不写入。STOP 立即关闭视频录制门控，固定设备时间上界，再等待 20ms 收取属于该时间区间的迟到 IMU；关闭回调的会话标签后等待已入队样本消费完成，最后关闭 MCAP。排空期间不接受下一个 START，不会把旧队列重新标记为新 episode。边界基于当前映射估计，精度受映射及单向接收延迟影响，超过 20ms 的迟到尾样本不会无限等待。
- 运行时状态 JSON（不是 episode metadata）记录回调处理数/溢出数、映射恢复数/ready、迟到样本数和相机单调保护次数；debug 启停日志包含会话设备时间上下界，正常 info 级别不输出逐段诊断。没有新增 metadata 字段。
- 可在 `ugripper.service` 的 systemd drop-in 设置 `Environment=UGRIPPER_CPU_PROFILE=fays_split`，重启服务后生效；安装服务默认启用该均衡分工，设置 `off` 可关闭；直接运行独立程序且未设置环境变量时保持原亲和性。RK3588 且原始允许 CPU 0–7 时，Runtime 及其普通子进程用 0–3，Fays/SDK 用 4–5，CameraRecorder/其编码线程及 Fays FFmpeg 用 6–7，SensorRecorder 普通线程用 0–7，左右编码器请求线程共享 4–5，避开小核辅助脚本启动和 6–7 视频编解码竞争。服务入口先将 shell、日志转发器及辅助进程放到 0–3，录制子进程在日志初始化后切换到各自核心组，因此日志线程仍在 0–3；服务外部进程保持原亲和性；此配置不独占或隔离系统 CPU。未知 profile/不支持的拓扑保留原设置并告警。亲和性在进程/编码器请求线程启动和 FFmpeg fork 时一次设置，没有周期性扫描、运行时迁移或每次 START/STOP 的重新分核；绑核本身不额外引入启停等待，但负载下调度延迟仍需实测。

### 辅助任务与视频线程开销

- Fays 守护健康解析由一个常驻标准库 Python helper 每 100ms 批量读取左右 runtime，标定按文件身份变化更新缓存；只写私有原子快照，不新增 episode metadata 字段。shell 按原始帧/IMU 时刻与原有阈值实时判断，快照年龄采用 BOOTTIME（与 `/proc/uptime` 同为 10ms 精度），超过 500ms 不接受。helper 卡住超过 1.5s 会重建，创建/恢复首份快照最多等待 500ms；正常控制循环不新增等待。解析器不可用与相机故障分开，不直接重启健康 SDK，录制中通过现有 session error 表达监控不可用；状态 JSON 仍独立检查实时数据。退出清理 helper，父进程消失时 helper 自行退出。状态写入及多设备 serial 检查仍有周期 Python 启动，后续继续合并。

- 闲置 USB 耳机本地播放及按键功能通过 `audio_common.py` 的 `USB_HEADSET_ENABLED = False` 禁用；audio daemon 保留 FIFO 生命周期但不启动后台 udevadm/pactl 探测，不伪造音频 ready。本地默认扬声器 fallback 同时停用，Ego 语音链路不受影响。恢复前须用事件通知或缓存替代缺席设备的周期扫描。
- 编码器非阻塞接收仅空读时等待 250µs，有数据时继续按 256 字节缓冲读取并在每次读取前检查停止；不修改批量时间戳重建语义。

### 编码器录制健康约束

- 编码器读请求使用单调时钟维持 1ms 绝对周期，允许迟到后补发，但每次串口写入完成后至少留 500µs 静默间隔，避免历史请求极度密集发送。该间隔不是采样时间戳补偿；真实调度/传输 gap 仍保留，不修改串口协议、MCAP/metadata 字段或断流保护。
- 周期性读请求通过 `sp_nonblocking_write` 发送，避免阻塞写 API 的 TTY poll 等待接收工作队列。短写只继续未发送的字节；零字节/短写重试之间等待 100µs，重试期限为 100ms，失败按已有串口断连路径处理。该期限不能中断单次内核写调用：CH9344 驱动仍可能在 tty_write 内等待，非阻塞 API 不等于没有驱动延迟。配置写入流程不变。

- SensorRecorder 任一侧初始化失败均取消起录；主运行时通过继承管道等待左右均已产生有效编码器样本，最多等待 5 秒，失败停止已启动的录制子进程并报 `encoder_init_failed` / `UGR-30-02`，不进入 Recording。
- 采集过程中使用单调时钟监测有效样本，任一侧连续 3 秒无数据或发生串口断连，报 `encoder_stream_stale` / `UGR-30-07`，排空写队列、关闭 MCAP 后退出，主运行时自动停录并保存失败原因。设备节点仍存在不能替代数据健康检查；此故障不主动触发全局 USB 断电恢复。
- 停录校验读取左右编码器末帧 Unix 时间，与同侧已启用视频的 Unix 起点加有效时长所得最晚末尾比较，最大允许落后 1 秒。topic/消息缺失、不可读、缺少参考时间或尾部不足报 `encoder_tail_missing` / `UGR-30-08`；适用于含双目与 `+nostereo` 两种包。该检查不替代逐帧 gap 深度校验。 查询要求 MCAP chunk 索引可用，只检查末尾最多 4 个 chunk；索引缺失或该窗口内找不到 encoder 消息直接报错，不回退全量消息扫描。

- Web 时间同步测试按 `/etc/environment` 的 `DEVICE_SN` 小写选择当前设备数据目录，并显式传入毫秒表脚本；未配置 SN 时使用运行时相同的 `noname_device`。不依赖数据盘仅存在一个设备目录；状态轮询保留每组日志展开和滚动状态。
## 2. 安装布局与入口

- Web/API 所有软件及固件升级要求背包电量至少 30%，BMS 数据不可用/超过 20 秒也拒绝；任务接收、停服及实际安装/逐侧烧录前检查，强制升级和充电均不豁免。Web 的 RK3588 设备固件入口接受包含 update.sh、SHA256SUMS、payload 的完整发布包，支持 PC 解压后选择整个发布目录上传；不再接受裸 boot.img。root worker 验证文件清单、硬件型号及 boot 分区后，以 sh -e 执行包内脚本；按 V100/V130 选择对应 FIT，脚本可同时更新驱动、启动脚本、日志配置及文件系统。目标版本、文件和 boot 分区回读一致后等待独立重启指令，同版本也执行完整包；重启前清理临时包，新 boot ID 下再次核对版本、文件和分区。Runtime 将 /home/user/version.info 缓存为 rk3588_firmware_version，Web/API 共用，起录冻结后写入 metadata 3.3；中断不自动重刷。

- 安装目录：`/opt/ugripper`
- 主服务：`pack_script/ugripper.service`
- 主入口：`/opt/ugripper/run_record.sh`
- 主运行时：`/opt/ugripper/bin/UgripperRuntime/UgripperRuntime`
- 独立维护 Web 服务：`standalone/UgripperWebServer/` 提供 HTTPS 客户页面，接网线即可免登录访问（无登录账号或会话，升级、参数保存及标零逐次校验密码），维护写请求保留跨站防护与操作确认，默认绑定以太网 `192.168.2.240:8080`，独立 `ugripper-webserver.service`（ubuntu）与 `ugripper-maintenance.service`（本地 root worker）。设备状态、日志下载、数据浏览/下载、白名单软件升级、左右主摄/夹爪固件升级、治具确认后的左右独立编码器标零通过受控 API 提供。主摄 iSerial 与夹爪固件版本按左右展示，由 UGripper 维护 `/dev/shm/ugripper/firmware_status.json`，Web 仅读取快照，概览左右设备标题后显示各自夹爪 SN（由已有身份缓存写入快照，断连或过期显示 —）；夹爪复用 HMI 驱动在空闲时读取，启动/重连自动刷新，失败每 30 秒限频重试，录制期间不查询；配置页支持空闲时查看左右编码器原始计数、角度与弧度；每次位置查询在录制/维护共享锁内打开并关闭串口，只发送 Modbus 读命令，支持 1Mbps/115200 且不改设备配置。四个环境参数（语言、编码、胸部主摄开关、电源频率）提供下拉配置，确认后由 root 专用 helper 限定写入 `/etc/environment` 并重启数采。录制与维护/下载共用 `/run/ugripper/maintenance.lock` 原子锁，持有到各自收尾结束；主摄升级固定调用包内 dfu-util。配置及测试说明见 `standalone/UgripperWebServer/README.md`。
- Web 配置页时间同步测试：可配置每组录制 1–3600 秒、1–100 组、毫秒表延迟 0 秒起且小于录制时长（默认 100 秒/1 组/延迟 2 秒）。复用包内毫秒表脚本，经 root worker 自动控制 GPIO139，每组前后高电平清零 5 秒，START/STOP 与 GPIO 实测边界写入 episode 的 `stopwatch_timing.json`；与生产压测/维护/下载互斥，断开网页继续执行，取消等待收尾，中断不自动续跑。采集完成不代表已通过光学同步标定。
- Web 修改性维护操作（软件/固件上传升级、设备参数保存、编码器标零）逐次要求维护密码，服务端先校验再接收上传或派发维护；不在浏览器保存密码，不传入任务状态。页面访问、状态/下载/抓图/检测/编码器读取及自动测试仍免登录；触觉基线重置只需弹框确认，无需密码；原有治具与互斥约束保留。
- 数据目录：`/mnt/data_disk/<device_sn_lower>/data`
- 持久化标定目录：`/etc/ugripper/config/calibration`
- 日志目录：`/var/log/ugripper`
- 运行日志镜像目录：`/mnt/data_disk/logs`
- 系统日志本机缓冲：journald `Storage=volatile`，`/run/log/journal` 预算 64MiB、单文件 8MiB；rsyslog 使用专用配置，写入 `/var/log/ugripper-syslog/syslog.log`，保留既有 syslog 文本模板，5MiB rename 轮转并预留下一段空间，合计上限 50MiB。本地缓冲可能淘汰尚未导出的旧日志；原先的 rsyslog 分类输出和远程转发规则不再加载。停止并屏蔽 log-persist/log-migrate 服务和 timer，移除案例 journal USB 软链、persistent 配置及 USB logrotate 规则；没有周期迁移或常驻 USB 系统日志输出。
- rsyslog 启动通过 `RequiresMountsFor` 等待本地日志路径挂载，并在 `ExecStartPre` 创建专用目录 `0750 syslog:adm`；新日志文件显式为 `0640 syslog:adm`，沿用运行用户的 `adm` 读取权限。不调整 `/var/log` 整体权限，不再依赖 `/tmp/logbuf` 或早期 tmpfiles。保留原轮转 helper，避免与系统 `/var/log/syslog` 的 logrotate 规则冲突；不新增系统日志定时任务。本地源游标按 machine/device/inode 与前缀指纹识别，重启不重复导出仍保留的文件；manifest 的 `ram_evicted_bytes` 字段仅为兼容旧清单，现表示本地源淘汰计数。新配置验证并提交成功后直接删除旧 `/tmp/logbuf`，不迁移旧日志；失败回滚时保留。轮转文件与活动文件只放在该专用目录，最多 9 个历史文件加 1 个活动文件，同时受 50MiB 总量约束。
- Runtime 空闲时预热事件型日志 worker，收到请求才导出 syslog；正常停录在本地文件关闭及双目 timing 合并后并行导出早期快照，完成 Ego 收尾与完整校验后补导末尾增量并执行前台刷盘；音频、卸载前及正常退出仍同步导出。worker 不可用时回退 `save_system_log.sh <diskRoot>`，不增加周期 USB 写入，只导出 syslog 文本；journald 继续在 RAM 运行，不轮转或复制原生 journal。输出 `/mnt/data_disk/system_journal/rsyslog/syslog.<卷ID>.<片段ID>.log`，每次仅保存新增完整行，末尾半行留到下次；固定本地文件描述符和长度，不追随新增内容、不清空源文件。单逻辑卷最多 100MiB/128 片段，最多 8 卷、总量 500MiB、最长 30 天；停录不全量重写或压缩。卷由清单按顺序引用不可变文本片段，排障/下载按清单顺序读取。本地尚未导出的日志可能被容量上限淘汰；syslog 在本机存储保留，但突然掉电前未刷盘数据不保证保留。
- `system_journal/manifest.json` 是唯一快照事务提交点，记录已提交文本片段、源 machine/device/inode/完整行偏移及前缀指纹、容量淘汰计数和盘身份。统一前台锁串行导出；只在本地文件发现/固定长度时短持轮转锁，复制及刷盘不阻塞源轮转。先 `sync -f` 再原子替换清单并同步目录，最后清理未引用文件并再次刷盘；失败不放行 Ready，重试按已提交清单恢复。前一轮实验的原生 journal 仅在新清单成功提交后清理。拒绝未挂载盘、盘身份中途变化及软链输出目录。仅导出 rsyslog 实际收到的消息，不保留 journal 结构化 unit/boot/单调时钟字段；转发排队或上游限流可能造成快照尾部延迟或日志丢失，本地淘汰计数不等于未导出数据的精确丢失量。
- 日志同步失败会锁存错误状态，健康恢复/提示音刷新不能提前放行 Ready；音频、人工标记及卸载也检查同步结果。Runtime 日志继续存 `/mnt/data_disk/logs/umi_sys_*.log`，与系统日志共同完成前台 `sync -f` 后才释放录制锁并恢复 Ready/绿灯；失败进入错误态。Web 日志下载读取清单固定的文本片段集，不为下载向 U 盘生成副本；旧文本 journal cursor 导出链路已移除。必要的磁盘刷写没有后台或超时放行，内核 D 状态不能保证固定时间退出。
- 系统日志配置由 `postinst -> install_ram_logging.sh -> ram_logging_config.py install USER` 应用：先检查用户、依赖、tmpfs 和候选 rsyslog/sudoers，再为每个文件执行同目录暂存、fsync、rename、目录 fsync；软链替换和删除同步目录。多文件及服务切换由 `/var/lib/ugripper/logging-config/` 下 root 专用原配置备份与持久 undo 记录保护，包含文件内容/权限/属主/软链、用户组增量及原活动服务。有效 journald/rsyslog 配置、服务状态和运行用户提权权限检查通过后才提交；失败或可捕获信号自动回滚，回滚失败保留 undo 供重试。启动恢复 oneshot 在 rsyslog/UGripper 前处理遗留 undo；持久恢复代码不依赖 dpkg 解包期间的 helper。配置被外部修改时拒绝覆盖。`prerm remove` 在包文件删除前事务恢复初装前配置；反复升级不覆盖原始备份。标准用户组工具负责系统组数据库锁和写回；该机制是可恢复的多文件事务，不能将多次 rename 宣称为单次全局原子操作。

- 终端硬件检查：`/usr/local/bin/hws`。HWS 与 Web 概览均显示 Ego 绑定状态、连接状态和绑定序列号，共用只读快照解析；绑定以 `/dev/shm/ugripper/ego_binding.json` 为准，连接要求状态快照序列号匹配且 15 秒内更新，否则显示未知。未绑定时连接显示未检测，不额外运行 ADB，也不把 USB 存储枚举当作 Ego 在线。
- U 盘升级入口：`auto_update/usb_auto_update.sh`

### 触觉相机采集时间保留

触觉 `hybrid-decode-encode` 使用 FFmpeg `-timestamps abs` 将 V4L2 时间映射为 Unix，`-fps_mode passthrough -enc_time_base -1` 保留输入时间基，不再按名义 120fps 自动补/丢帧。名义帧率仅请求设备采集模式。`debug_ts` 的 `demuxer+ffmpeg` 精确整数 `off` 建立 Unix 起点，禁止使用低精度科学计数的 `off_time`；只把 mux stats 计为输出帧，逐帧调试行被消费而不写运行日志。缺少有效 Unix 起点不回退为编码输出接收时间。当前逐路固定补偿见 7.2.1；旧 −35ms 包含编码输出观测延迟，不能沿用。容器毫秒量化与实际 USB/V4L2 时间抖动保留，不进行平滑。

双目起录等待 MCAP 打开后再允许编码/IMU 提交。停录先禁止新的会话提交，通过共享/独占锁等待在途编码与 IMU 提交结束，刷新生产者本地批次并关闭视频输入；MCAP 写线程排空当前会话的本地批次与队列后关闭文件并确认，下一轮才可开始。进程退出也须等待生产者全部结束后让 MCAP 写线程退出，避免只因队列暂空而提前关闭。SDK 采集线程不持有编码收尾锁。

停录时本地 recorder 和双目关闭、timing 合并后，在等待 Ego 的同时预校验本地 video/sensor/stereo；最终仍执行完整校验。仅 ffprobe 结果可复用，按设备号、inode、大小、mtime 和 ctime 纳秒身份检查缓存，文件改变必须重新探测。Ego STOP 目录轮询间隔限制为 50–150ms，录制中的增量轮询周期不变；最终拉取、复核及 header refresh 使用批量文件大小查询。拉取后的新鲜远端大小和所有本地文件一致时省略第二轮补拉；缺失或不符仍走原恢复重试。远端删除前再次检查大小，保留 MP4 header 刷新、修复和同步写回。

Ego STOP 子进程在 finalized 后通过控制 socket 确认，释放写锁并等待主机完成数据刷盘；主机再通知同一子进程清理远端并等待退出，避免第二次解释器启动。主机关闭控制通道、未知命令或原 STOP 预算耗尽均保留远端，不主动删除；日志快照和前台最终同步全部完成后才 Ready。早期快照失败仍执行最终补导，但本次停录保持日志错误门禁，下一次成功同步才可恢复。

Ego 停录 MP4 检查按 box 头读取并 seek 跳过媒体数据，普通 mdat 立即结束该兼容检查；64 位 mdat 长度异常时仅在文件尾做有界搜索，每块 64KiB、总读取最多 16MiB、最多解析 2048 个 box。只在 moov 结构及至 EOF 的后续 box 链可验证且候选唯一时修复 8 字节长度，随后同步 fsync 并回读；不能安全判断或刷盘失败则收尾失败、保留远端副本，不降级为成功。Ego 状态保存各停录阶段及逐文件补拉/fsync 耗时；MP4 检查记录读取量和耗时，Runtime 停录阶段与 flush 边界日志无需开启全局 PERF，超过 1 秒的单文件刷盘记录告警。同步刷盘等待仍可能超过循环检查期限，不能承诺固定停录时长。

按现场反馈，部分停录蓝灯等待来自 Ego App：收到 STOP 后，App 生成 metadata 时通过厂商 SDK 查询头带固件版本，失败重试本次观察额外约 11.4 秒。该值不是固定延迟；需结合 App 日志区分其版本查询与 UGripper 的补拉、校验及刷盘耗时。测试清单见 `docs/v2.1.14-test-checklist.md`。

- root Web 维护代码的 production/bin 在打包 staging 中使用 rsync --delete 原位更新，避免目录嵌套导致升级后仍导入旧模块。
- 主摄升级独立读取镜像版本与所选设备版本，镜像检查不依赖摄像头在线；仅明确 CLI 参数/usage 错误才兼容旧命令，设备缺失或镜像读取失败不重试，保留原命令、退出码及输出。Web 缺摄预检和设备版本错误显示具体 /dev/cam_left 或 /dev/cam_right，避免误报镜像失败。
- 主摄升级前或刷写后设备版本读取失败时，Web 弹窗等待确认（最多 5 分钟）；确认并输入维护密码后可跳过该次读取继续，取消或超时终止后续操作，已刷写固件不回滚。等待期间保留维护锁且数采保持停止。镜像版本读取失败、已读版本不匹配以及端口/SN/MaxPacket 检查失败仍终止；跳过刷写后版本验证时结果明确标记“设备版本未验证”，日志保留失败原因与选择。同版本覆盖选项不自动授权跳过读取失败。

## 3. 模块总览
| 模块 | 入口 | 当前职责 | 关键输入/输出 |
| --- | --- | --- | --- |
| systemd 主服务 | `pack_script/ugripper.service` | 以 `ubuntu` 用户拉起录制服务 | `/opt/ugripper/run_record.sh` |
| 薄壳启动脚本 | `run_record.sh` | 切到安装目录、维护本地 `/tmp` 到 `/mnt/data_disk/logs` 的增量日志同步，再拉起 `record_runtime`；数据盘等待由 runtime 处理 | `/tmp/umi_sys_<sn>_<date>.log`、`/mnt/data_disk/logs/umi_sys_<sn>_<date>.log` |
| 主运行时 | `bin/UgripperRuntime/UgripperRuntime` | HMI 按键状态机、软件录制控制、LED 灯效、提示音、pre/post 音频、camera/sensor 子进程管理、停录校验、关机请求；SIGINT/SIGTERM handler 只设置原子停止标志，ego 与其他子进程统一由主循环退出后的正常收尾路径停止 | episode 目录、`/dev/shm/ugripper/umi_record_control.pipe`、`/tmp/umi_shutdown_request` |
| 安全 NTP 同步 | `time_sync/safe_ntp_sync.sh` / `ugripper-ntp-sync.service` | 开机或安装后只在非录制态执行一次性时间同步，优先使用板端现有 `sntp -S`，再 fallback 到 `ntpd -q -g` / `timedatectl`；同步完成、超时或录制开始后停止常驻 NTP 服务 | `/tmp/umi_recording.lock`、`sntp`/`ntp` |
| 相机录制 | `bin/CameraRecorder/CameraRecorder` | 负责主摄/触觉会话录制；默认同时启动 Fays 双目链路，`+nostereo` 变体不启动 Fays | 默认 8 路基础 `mkv`；`+nostereo` 为 6 路 |
| 传感器录制 | `bin/SensorRecorder/SensorRecorder` | 录制左右 IMU/encoder，按“采样入队 + 每侧独立 MCAP 写线程”分别输出 MCAP；收到 `SIGINT` / `SIGTERM` 时信号处理函数只设置停止标志，退出主循环后先等待编码器读写线程停止，再关闭串口、排空队列并关闭 MCAP，避免信号上下文日志死锁或串口释放竞态造成 Footer 缺失；encoder zeroing 使用相同的信号安全约束 | `sensor_left.mcap`、`sensor_right.mcap` |
| Ego 联动采集 | `bin/UgripperRuntime/ego/ego_recording_worker.py` + `bin/UgripperRuntime/adb/adb` | 通过左右上键组合绑定/解绑一台 SXR Ego；录制起停阶段通过内置 ADB 联动 Ego app，绑定后持续同步系统时间，起录前校验同步就绪，并按增量方式同步 Ego episode。未绑定时保持可选联动；绑定后设备在线、停录 finalize、文件拉取和远端/本地大小核对均成为录制硬条件；绑定只在本次开机周期内有效，整机重启后自动解除 | `/dev/shm/ugripper/ego_binding.json`、`/dev/shm/ugripper/ego_binding_status.json`、`ego/`、`ego/ego_sync.json` |
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
- HMI 状态查询、LED RGB 与蜂鸣指令会在单个 gripper 串口内由驱动 owner 线程统一调度；`record_runtime` 只切换灯效模式，不再跨线程推送录制态的 500ms 亮灭边沿。所有周期灯效直接使用同一个绝对单调时钟计算相位，左右夹爪不会因状态恢复时刻或单侧重连而独立重新计时。当前驱动参考旧版 `driver_origin/led_manager` 的职责分离思路做了收敛：按键输入仍以 HMI 主动上报 `KeyReport` 为主，串口主动查询已降为约 `1s` 一次的低频探活/状态刷新；LED 仅在颜色变化、状态切换或低频补发时下发，避免高频状态查询与 RGB 指令互相抢占；蜂鸣开启和关闭状态切换都会按 `20ms` 重试计划连续下发 `5` 次（实际发送还受所有帧的25ms静默门禁约束），开启后仍维持原有保活，关闭状态在初始重发后每秒补发 OFF，HMI 连接建立和驱动退出时也会主动重发关闭状态。Ego 绑定、解绑和绑定失败反馈结束后还会按侧请求最新蜂鸣状态，回读 `duty=0` 即确认关闭（允许固件保留旧 frequency），否则最多重试 `5` 次，避免最后一个关闭帧丢失后长期蜂鸣。若有专项诊断或直控需求，仍可走 direct RGB 通道覆盖当前效果。
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
- HMI owner 按实际发送完成时刻计算静默与重发期限：显式蜂鸣/静音优先、到期状态查询次之，RGB 只保留最新目标；重复保活每轮至多发送一个，按到期时间仲裁。底层所有发送（含显式蜂鸣、RGB、状态查询和独占维护命令）共用每串口帧间隔门禁：上一帧写入后有界检查主机发送队列排空（最多50ms，失败按串口I/O故障处理），再预留该帧8N1线速发送时间和25ms静默，下一帧才允许写入；分别调用write不再被当作帧间隔保证。该静默是主机侧规避，不代表固件执行ACK或已验证1.0.8物理UART接收；真实线间隔仍需抓线确认。保留 25ms 蜂鸣/查询静默、5 次可靠性发送与原有周期；条件变量按可执行期限等待并释放锁，不因 pending 查询尚在静默期而忙转。接收检查计划等待最多 20ms，新命令/停止通知可提前唤醒；该上限不是内核阻塞和 OS 调度延迟的保证。HMI 与 encoder 不共享等待应答的全局锁。
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

左右上键采用精确按键掩码路由：两键形成组合后会抢占并重置各自的单键状态，额外按下左下或右下时不匹配该组合；组合执行后必须全部释放才允许下一次动作。这样第二键可在普通长按的 `1000ms` 容错窗口内加入，而不会同时触发 pre audio、人工失败标记或短按录制动作。组合绑定开始时播放 `1000/2000/4000Hz` 升调，等待同步期间双手黄灯闪烁并循环三短音，同步达标后停止蜂鸣并恢复就绪灯效；解绑成功为双手三次紫色短闪，并依次播放 `4000/2000/1000Hz` 降调；绑定失败保持双手三次红色长闪与三次长蜂鸣。同步等待结束由驱动后台重复发送关闭蜂鸣命令，不阻塞就绪切换；解绑和绑定失败反馈仍逐侧回读确认关闭，再恢复当前灯效。

Ego 绑定文件位于 `/dev/shm/ugripper/ego_binding.json`：同一次开机周期内重启 `ugripper.service` 或重装软件仍保留绑定，整机重启或断电开机后 `/dev/shm` 清空，因此自动恢复为未绑定状态。`ego_binding_status.json` 使用 `bound / connected / disconnected / unbound` 表示绑定完成、在线、离线和已解绑；服务启动时若没有有效 binding，会主动发布 `unbound`，清理旧版本遗留的 `connected / disconnected` 快照。解绑不依赖 ADB 在线，即使 Ego 已拔出并进入 `disconnected / ERROR_5`，仍可通过左右上键组合解除绑定并清除该故障。解绑会在共享锁保护下删除 binding 并原子发布不带旧 serial 的 `unbound`，在途的旧 probe 在发布结果前会重新核对当前 binding，不能把状态覆盖回旧 serial 的 `connected`。解绑后不再探测 Ego，因此随后物理拔出不会改变 `unbound`。安装新版本时会清理旧 `/var/lib/ugripper/ego/binding.json`，避免历史持久化绑定被重新加载。

Ego 起录使用 Runtime 直接持有的预热 Python worker：已绑定且空闲时提前完成模块加载和身份/路径准备，通过继承的私有 `SOCK_SEQPACKET` 控制端接收一次 START；同一 PID 随即成为本条 episode 的增量 owner，不创建孙进程或独立全局 IPC 服务。空闲不创建 episode、不持有录制/维护锁，也不访问 U 盘；控制端 EOF、解绑或 Runtime 正常退出会清理预热进程，Runtime 崩溃时由父退出信号终止它。控制 fd 使用 CLOEXEC，只将 worker 端显式传入目标子进程。派发前 worker 已死亡或派发失败时回退常规子进程起录；成功派发后不自动重发 START，等待现有状态确认及失败收尾。一次会话结束后在下一次空闲期补充预热进程；录制中的 2 秒无进度退出、2.5 秒 watchdog、SIGTERM/按阶段 SIGKILL、resume、STOP 补尾和同步刷盘完成后 Ready 的 ownership 不变。

身份及路径缓存只保存在 `/dev/shm/ugripper/ego_binding.start-cache.json`。每次 START 重新核对绑定 serial/`bound_at_ms`、ADB `transport_id`、设备 boot ID、`pm path` 和 APK inode/size/mtime/ctime，并固定本条 ADB 命令使用该 transport；App 升级/重装、USB 重连、设备重启、重新绑定、目录不存在及探测/起录失败都会使相应缓存失效，完整属性/包版本/目录发现移到首次预热或失效恢复。正常起录不执行 `dumpsys package` 和目录 `find`；5 个硬件属性以单次 `getprop` 读取，会话内沿用同一 root 集合并只查找新增临时目录。App PID/进程 starttime 变化仅清除 codec 缓存；同一 App 进程、transport 和相同目标编码已成功设置时省去 `SET_VIDEO_CODEC`，编码切换或身份变化重新设置。此缓存依据 UGripper 目标值和 App 进程指纹，不在每次 START 查询 App 内实际设置；同一 App 进程内人工改 codec 且没有设置变更事件时不会主动失效。Android 不支持 APK/proc 指纹读取时清除缓存并回退完整检测，不放宽身份、真实录制状态、旧会话恢复或授时新鲜度检查。`ego_sync.json` 保存 `start_cache_hit`、各起录准备阶段 `start_timings_ms` 和 codec 缓存命中信息；这些是软件阶段耗时，不等于实体按键或声音播放时序。

Ego 绑定时通过指定 serial 的 `dumpsys package com.ssnwt.helloxr` 读取 `versionName`；数字版本达到 `1.0.4` 才允许电流配置，版本缺失、无法解析或低于门槛时跳过新增功能并保留绑定。版本支持普通三段版本及可选 `v/V` 前缀，带其他后缀的版本保守视为未知。绑定文件追加 `app` 与 `input_current`，记录版本、能力、电流配置及广播回读结果。电流直接读取 UGripper 主机 `/etc/environment` 的 `HARDWARE_VERSION`，仅 `2.5.x > 2.5.0` 设置 `1600000 µA`（1.6A）；缺失、非法、`<=2.5.0` 和其他系列统一 `1100000 µA`（1.1A），不使用 metadata 的 `UGRIPPER_HARDWARE_VERSION` 默认值。绑定时发送显式 `InputCurrentLimitReceiver / SET_INPUT_CURRENT_LIMIT` 广播，并核对 JSON 的状态、callback、requested/readback、matched 和 persisted；失败只告警，不取消绑定，也不改变录制硬条件。

Ego 联动语音使用后台串行队列发送 `SpeechCommandReceiver / SPEECH_PLAY`，不阻塞采集与停录收尾。`1003` 在录制后的数据写入/收尾阶段持续超过 5 秒时发送一次，提前结束取消；成功起录超过 3 秒且数据盘可用比例严格小于 5% 时发送 `1203`，每条录制一次，其他磁盘故障不代用此播报。`1204` 随 Error1 的 `validation_failed` 播放，连续三次触觉异常的现有告警触发 `1205`。起录前 3 秒暂缓队列发送，避免打断 Ego 起录提示。仅向已绑定且在线的 Ego 发送，未绑定直接跳过；App 低于 `1.0.4` 或未知则跳过，发送前再次校验绑定。语言读取 `/etc/environment` 的 `UGRIPPER_LANG`，缺失回退进程环境；`en` 使用英文，其余为 `zh`。`accepted` 仅表示 App 接受入队，不表示播放完成。人工标记无效不触发 `1103`。

BMS 串口由蓝牙服务的 `update_bms_data` 独占采集，UGripper 不再打开或配置 `/dev/ttyS0`，只每秒读取 `/opt/bluetooth_server/bms_latest.txt`。校验必需字段、整数/布尔值、SOC 范围和源 `timestamp`，超过 20 秒、时间在未来、缺失或内容不完整均不可用，下一轮有效快照自动恢复；重复读取不延长旧样本寿命。UGripper 原子发布 `/dev/shm/ugripper/battery_status.json`（`source/status/connected`），Web 每 3 秒刷新电量及充放电状态；读取异常显示“电量不可用，请检查蓝牙电量服务”，不显示 0% 或缓存旧电量。此时应检查 `bluetooth_gatt.service` 和源快照时间戳，不能通过让两个进程并读串口解决。`1302` 在电量下降到 10%、5%、4%、3%、2%、1% 时各播报一次；跳过多个阈值只合并提示一次，充电期间不提示，充电回升至 10% 以上重新布防。去重状态为进程内状态，重启后按当前有效电量判断。无有效电量不触发告警。

| 状态 | 中文 | 英文 | code | 设备类型 |
| --- | --- | --- | --- | --- |
| 背包低电量 | 背包电量低，请充电 | Backpack low battery. Please charge. | 1302 | Umi |
| 数据记录/收尾超过 3 秒 | 数据记录中，请稍等 | Data recording ... Please wait | 1003 | Umi |
| 数据盘可用空间低于 5% | 存储容量不足，请及时清理 | Storage space is running low ; pleaseclear. | 1203 | Umi |
| 标定数据异常/时序不完整（Error1） | 数据校验错误 | Data validation error | 1204 | Umi |
| 连续三次检测到触觉图像异常 | 请检查触觉传感器表面是否破损 | check the surface of tactile sensor for any damage. | 1205 | Umi |

对照表保留 Ego 提供的文案；UGripper 发送 code 和语言，不直接发送或合成文案。


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
7. Ego 未绑定时，起录前 `record_runtime` 保持原可选联动：先用随包 ADB 做短窗口在线设备预检，明确无在线设备时直接跳过 sidecar；预检发现设备或结果不确定时拉起 worker，短等待内未拿到远端 episode 只记录告警，不阻塞 UGripper。Ego 已绑定时只允许使用持久化 serial，绑定设备不在线或未成功拿到本次远端 episode 会阻止起录并进入 `ERROR_5`。worker 默认优先使用随包安装的 `bin/UgripperRuntime/adb/adb`：设备识别要求 `ro.product.manufacturer=SXR` 且 `ro.product.model/ro.product.device/ro.product.name/ro.build.product` 均为 `SXR_1`。绑定后由独立授时服务先做一次 ADB 粗授时，再持续 USB 精同步；起录本身不设置 Ego 时间。Runtime 在绑定/同步未就绪时拒绝录制，不创建 episode，worker 仍复核本次绑定及同步快照的新鲜度；若准入后同步失效则写入 `time_sync_failed` / `ego_time_sync_failed`，输出 `UGR-82-02` 并终止本条起录。同步标准和绑定声光见“Ego 绑定后持续 USB 校时”。校时通过后才按 `CAMERA_CODEC` 确认或广播 `SET_VIDEO_CODEC`（`h264 -> avc`，`h265 -> hevc`），再向已运行的 `com.ssnwt.helloxr` app 广播 `START_RECORDING`。worker 从 app external-files 根目录发现新建的 `episode_*-temp`：显式配置的 `UGRIPPER_EGO_REMOTE_ROOT` 优先级最高；未配置时把绑定 serial 的新版 `/files/dataset/<ego_serial>/data`、旧版 `/files/<ego_serial>/data/dataset`、legacy `/files/dataset` 与 fallback 扫描结果统一汇总去重，再从全部候选 root 查找本次新增 episode，避免空的旧目录遮蔽实际写入路径。远端文件增量同步到本条 episode 的 `ego/`，当前仍不主动启动 Ego app。
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
- 当前随包 vendored Fays VIKit SDK 为 `3.8.4`，来源为上游 tag `v3.8.4` 提交 `1bb5d83d111f25bf29dac5e081312829c1a941eb`；同步 aarch64/x86_64 运行库与公开头文件，FT602 仍为 `1.0.17`。左右 SDK YAML 均设置 `imu_v4l2_buffer_count: 64`，扩大 IMU V4L2 接收缓冲以缓解高负载或调度延迟下的缓冲溢出，双目图像 buffer 和应用队列保持不变；实际分配及丢帧改善需板端验证。具体来源见 `src/third_party/fays_lib/UPSTREAM.md`。
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
1. `record_runtime` 在进入 STOP 时立即异步启动 Ego stop worker（本条未启动 Ego 也发送保险 STOP），随后并发停止普通录制模式下的 `camera_recorder` 与 `sensor_recorder`，Ego 广播与本地停录重叠。
2. 默认包同时发送 Fays `STOP` 并等待 stereo/Fays finalize；`+nostereo` 变体跳过这些动作。
3. 先发送 `recording_stop`，随后立即切到 `writing`；`writing` 会抢占仍在播放的上一条提示。
4. Ego stop worker 不等待增量排空就广播 STOP，并通过请求文件通知增量 worker 协作退出；延迟启动的 start worker 检查请求后不得再发送 START。增量 worker 取消当前读取，完成在途完整块的追加/fsync 与状态写入后退出，释放 episode 独占锁后由收尾 worker 补齐尾部。增量传输使用 `adb shell -T -n`（shell_v2、无 PTY），每次读取最多 1MiB，完整块才追加并同步 fsync；每 50ms 检查取消，连续 2 秒无数据则退出并返回重试码。Runtime 根据共享内存读取进度检测 2.5 秒停滞，先 SIGTERM 并等待 1 秒，仍不能退出且不处于追加/fsync 时才 SIGKILL；录制中从已写偏移恢复同一远端 episode，不重发 START，停录后只转入收尾。录制中的增量同步与本地 recorder 并行，但单个 Ego worker 仍按文件顺序拉取，并未改为多文件并发。Runtime 从发起 STOP 起设置同一个 60 秒单调时钟截止时间，覆盖广播、排空、远端 rename 等待、尾部补拉/重试、核对、MP4 头刷新/修复与后续远端清理；ADB 每次调用按剩余预算限制。Runtime 等待子进程只使用剩余预算，退出后读取一次终态，取消额外的状态轮询等待。超时不报告 finalized；保险 STOP 也必须先回收子进程再返回。超时后可能增加信号升级和进程回收时间，磁盘 fsync/内核不可中断 I/O 不承诺硬性 60 秒完成。远端清理预算不足时跳过并保留副本。STOP 广播最多发送 `3` 次，失败重试固定间隔 `100ms`，每次 ADB 状态、广播返回码和结果写入 `ego/ego_sync.json`；只要 Ego 已实际进入 recording，三次 STOP 全失败或设备消失都写 `ego_disconnected`，即使运行时绑定快照丢失也会使 episode 失败并进入 `ERROR_5`。finalize 后先按本地文件长度从 Ego 远端 offset 续传，落盘后核对全部远端/本地文件大小；不一致时重新检查远端数据并有限重试，远端缩小或本地大于远端时仅重拉异常文件。设备不可用归类 `ego_disconnected`，设备在线但远端文件缺失或重复拉取仍不一致归类 `ego_pull_incomplete`；绑定模式下两者均为录制硬失败。只有最终大小一致才尝试清理远端 episode，远端删除失败或 cleanup 跳过只记录状态，不覆盖主结果，也不单独使 episode 失败；不新增 MP4/MCAP 深度内容校验。
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
`device_sn / device_type / device_mode / camera_codec / hardware_version / software_version / das_usb_updater_version / data_version / bluetooth_gatt_server_version / databot_device_joint_version / device_ota_mender_version / gripper_left_firmware_version / gripper_right_firmware_version / cam_left_firmware_version / cam_right_firmware_version / cam_chest_firmware_version / stereo_left_firmware_version / stereo_right_firmware_version / ego_bound / ego_software_version / ego_firmware_version / rk3588_firmware_version / hardware_list / episode_name / data_uuid / audio_uuid / quality_check_status / quality_check_err_type / collection_duration_s / task_start_s / task_stop_s / boot_time_s / tactile_damage_sn_list / require_files / video_details`。

- `device_sn` 使用主控 `DEVICE_SN`，写出前统一转大写；`device_type=ugripper`，`device_mode=dual`，`data_version=3.3`。
- `boot_time_s` 是本次系统开机时间点的 Unix 秒时间戳，优先复用 CameraRecorder timing 中的 `boot_time_offset`；timing 缺失时按当前 Unix 时钟与单调时钟差值回退计算。
- `tactile_damage_sn_list` 初始写为 `[]`；停录时与 Ego 收尾并行的触觉分析中，任意单次 finding 满足 `damaged=true` 就加入对应非空传感器 SN，不等待连续三次黄灯告警门槛，SN 去重后在 Ready 前原子回写并刷盘该字段；本条 episode 未发现损坏时保持 `[]`。
- `task_start_s` / `task_stop_s` 是录制中右手上键任务切片点的 Unix 秒时间戳，按触发顺序各只写一次；当前 `3.3` 录制端未标记时固定写 `0.0`。`boot_time_s`、`tactile_damage_sn_list` 依次紧跟在 `task_stop_s` 后，并位于 `require_files` 前。离线深度校验允许历史或外部数据省略两个 task 字段；只有有效 `task_start_s` 而没有有效 `task_stop_s` 时按“无结束前回环数据”处理，不降低校验状态。
- `metadata.json` 使用范本顺序写出顶层字段、`hardware_list` 字段和 `video_details[]` 字段；校验脚本会把 key 顺序漂移作为格式错误。
- `data_uuid` 使用系统随机 UUID；若 `/proc/sys/kernel/random/uuid` 不可用则回退 `libuuid` 的 `uuid_generate/uuid_unparse`。无音频文件时 `audio_uuid` 允许为空字符串。
- `hardware_version` 优先读取 `/etc/environment` 的 `HARDWARE_VERSION`，保留 `v` 前缀；缺失时兼容 `UGRIPPER_HARDWARE_VERSION`，最后默认 `v2.5`；`software_version` 来自 ugripper 主包版本并带 `v` 前缀；`das_usb_updater_version` 优先读取 `das-usb-updater` 包版本。
- metadata 3.3 在 data_version 后包含 13 个版本字符串（新增 rk3588_firmware_version 位于 ego_firmware_version 后、hardware_list 前，来自 /home/user/version.info，复用现有版本缓存；历史 3.3 缺少该新增字段仍兼容）；ego_bound（布尔值）紧邻 ego_software_version 之前，hardware_list 末项为 ego_sn（未绑定为空）。主摄固件版本取 USB iSerial；胸摄未启用、+nostereo 双目固件写 "disabled"；未绑定 Ego 两个版本为空。起录前所有启用设备版本必须已知，缺项拒绝创建 episode，待机继续限频重试；起录冻结版本和绑定 SN，停录只消费内存副本。其余 metadata 字段、顺序、类型及 calibration 格式保持不变。Ego 拉回并完成远端大小校验与清理后，在本地 ego/metadata.json 写 related_episode_name 和 related_episode_uuid，分别等于 UMI episode_name（不带 -temp）和 data_uuid。 范本：`.codex/skills/validate-episode-data/references/metadata-v3.3.json`。
- 版本来源：已安装 dpkg 包、夹爪待机固件缓存、主摄 USB iSerial、Fays calibration.firmware_version、Ego 绑定缓存 app.version_name/firmware.version；复用现有状态接口，不新增版本快照 JSON。
- `hardware_list` 保持固定 schema：左右 gripper、左右主摄、可选胸部主摄、四路 tactile、左右 stereo 与 Ego SN 字段始终存在，未绑定时 ego_sn 为空；默认包从 Fays 状态写 stereo SN，`+nostereo` 变体写空字符串。
- `quality_check_status` 取值为 `success` / `fail` / 空字符串；`quality_check_err_type` 保持单字段兼容，只写本次失败的主 `error_type`。常见取值包括 `missing_file`、`collection_duration_too_short`、`frame_loss`、`stereo_frame_loss`、`finalize_error`、`stereo_control_failed`、`calibration_error`、`runtime_error`、`device_disconnected`、人工补标的 `operator_marked_failed`，以及健康监控直接上报的 `disk_mount_lost`、`disk_not_writable`、`disk_full`、`critical_devices_missing`、`stereo_not_ready`、`hmi_*` 等 fault key；无法归类时写 `unknown`。
- `collection_duration_s` 取有效视频时长最大值；默认包包含 stereo，`+nostereo` 只统计主摄/触觉。stereo 的有效时长固定使用 Fays MCAP 中首末 camera `publishTime` 的跨度，因此 Fays 设备侧 gap 会保留在实际采集时长内，不使用被固定 `25fps` 压紧的 MKV 容器 duration；`video_details` 复用停录 timing 缓存。
- `video_details[].start_offset_us` 保存各路补偿后的绝对 `record_time_offset_us`，单位为 Unix 微秒，语义为“该视频 `PTS=0` 对应的补偿后主机 Unix 时间”，不再减去本 episode 最早视频 offset。H.265 主相机按 `start_offset_us + packet_pts_sec * 1000000` 还原 Unix 时间：首关键帧 `start_offset_us=Tset+Pfirst-Pset`，packet PTS 来自当前帧换算 Unix 时间减去首帧 Unix 时间，因此任意帧 Unix 时间等于 `Tset+Pframe-Pset`，并保留编码器时间轴上的真实 gap；H.264 主相机继续使用 V4L2 buffer 时间生成同一表达。触觉相机仍由 FFmpeg 录制链生成 PTS 和首帧 offset，目前只保证处于 Unix 时间域并执行单调/gap 校验，不把它表述为硬件曝光时间真值。
- stereo MKV packet 与 Fays MCAP `c` topic 通过 `frameIndex`/顺序一一对应；stereo 精确逐帧 Unix 时间只使用对应已补偿的 `c.publishTime`，`video_details[].start_offset_us` 仅与首个 camera `publishTime/1000` 对齐。Stereo MKV 保持固定帧率播放时间轴，设备 gap 不要求由 MKV PTS 表达，也不得用 `start_offset_us+stereo PTS` 替代 MCAP `publishTime`。Fays 原始 `logTime` 只用于设备侧 gap/回退分析，不能作为全局 Unix 时间。
- 默认包的 `require_files` 包含 stereo/Fays 文件；`+nostereo` 变体只包含 metadata、calibration、左右主摄、四路 tactile 与左右 sensor。启用胸部主摄时两种变体都追加 `cam_chest.mkv`。
- 离线 episode 深度校验以 `software_version` 中的 `+nostereo` 为构建 profile 标记，并交叉核对 `require_files`、`video_details` 与实际文件；普通版本缺失 stereo/Fays 仍判失败，不能通过删减 `require_files` 自动降级。当前标准版本为 `3.3`，历史 `3.0/3.1/3.2` 作为兼容数据允许分析，其他未知版本给出告警。

#### 7.2.1 相机时间戳与延迟补偿

相机延迟补偿按 recorder 模块分别维护：主摄与 tcam 的数值入口是 `standalone/CameraRecorder/include/camera_recorder/camera_timestamp_offsets.h`，stereo 的数值入口是 `standalone/FaysStereoRecorder/include/fays_stereo_recorder/camera_timestamp_offsets.h`。后续实验接受新的延迟值时，只修改对应头文件中的微秒常量并重新编译；业务代码不得再写独立的补偿数字。补偿值为正时统一加到对应的全局 Unix 时间上。

| 相机 | 原始时间来源 | 全局时间表达 | 补偿位置 | 当前值 |
| --- | --- | --- | --- | --- |
| 左右/胸部主摄 | XU `Tset/Pset` + H.264/H.265 SEI `Pframe` | `start_offset_us + PTS` | CameraRecorder 写 `<camera>_record_time_offset_us` 时只调整绝对起点 | 左 `+7000us`，右 `+7000us`，其他 `+8600us`（2026-09-24-1632 固件） |
| 四路 tcam | V4L2 采集时间经 FFmpeg 转换到 Unix，保留输入时间基 | `start_offset_us + PTS` | 采集 Unix 起点；不包含编码输出日志接收延迟 | 四路统一 `-2000us` |
| 左右 stereo camera | Fays camera 设备时间换算到 Unix | Fays MCAP `c.publishTime` | 图像采集后、进入 MCAP 队列前逐帧调整 | 左 `-1000us`（未重新标定），右 `0us` |
| 左右 Fays IMU | Fays IMU 设备时间换算到 Unix | Fays MCAP `i.publishTime` | 不补偿 | `0us` |

主摄在起流前及每 10 秒经 XU 授时；每帧按自身 SEI 对应的 `Tset + Pframe - Pset` 映射 Unix 时间。H.264/H.265 共用该映射，详见下文主摄 SEI 与周期修正规则。
主摄打开设备后读取 `/etc/environment` 中的通用配置 `UGRIPPER_POWER_LINE_FREQUENCY`，支持 `50`/`60` Hz 并映射到标准 V4L2 `power_line_frequency` 控制；安装包在缺少该变量时写入默认值 `50`，已有值不覆盖。参数命名保持通用，当前仅主摄录制链使用，不改变录制 PTS 计算。

左右主摄与四路触觉采用 episode_20260928_0030 去共模标定结果，经确认分别统一为 +7ms 与 -2ms；排除错误的 0026 标定。双目保持既有补偿。仅适用于当前标定设备及条件，不代表所有设备。共模漂移不写入设备时间轴。主摄/tcam 固定补偿不修改 MKV packet PTS、gap 或 duration；Stereo 只改变 camera `publishTime`，不改变设备 `logTime`、IMU 或视频 PTS。metadata 从已补偿的 camera 时间建立起点，禁止重复补偿。

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
- 初始化 Web 配置与 HTTPS 证书：配置缺失、空白、非法 JSON 或非对象时直接重建默认配置；证书/私钥缺失、为空、损坏或不匹配时成对重建，不备份。保留有效配置与证书，配置经临时文件同步落盘后原子替换；权限、存储及证书生成错误仍使安装失败。
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
- 离线校验器分别识别 metadata 3.3（兼容 3.0/3.1/3.2）和 calibration 3.1（兼容 3.0）；后者检查独立外参标定状态、左右主摄两项有限数值 4×4 刚体矩阵及对应版本的严格字段合同。`uncalibrated` 与合法默认矩阵可通过格式检查，不代表光学精度达标；未知 calibration 版本、缺项或非法矩阵仍失败。
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

H.264 与 H.265 分别使用 `MainCameraH264CadenceTracker`（`main_camera_cadence.h`）和 `MainCameraH265CadenceTracker`（`main_camera_h265_cadence.h`），普通模式门限及相位状态独立，H.265 普通模式保留 V2.1.12 判定规则。两者共用延迟 I 帧对修正、Unix 映射和单调性保护，连续性解析仍分别使用 H.264 frame_num / H.265 POC 与 V4L2 sequence。仅在成功配置 50Hz 且输出 60fps 时启用。

普通模式修正重复帧及随后两真实帧为前一真实帧 encoder 时间 +1/+20000/+40000µs，再分别使用每帧自身 SEI 锚点映射 Unix。两种编码均使用最多三帧 lookahead，复用“延迟真实帧 + 短间隔重复帧”分支：完整时间窗口与 P 帧相对大小同时满足时，从更早真实帧建立 +20000/+20001/+40000/+60000µs 的四帧窗口。延迟真实帧为 I 帧时，重复候选必须为 P 帧且小于随后两个 P 帧中较小者的 35%，不比较 I 帧大小；H.265 仅启用此 I 帧模式，H.264 同时保留 P 帧模式。命中后清除旧相位，不传播补偿量；原始 SEI 不修改，未知模式及未确认停录尾部保留原始时间，沿用 250ms 排空和有界停止。H.265 相比原实现多等待一帧，避免 I 帧已经输出后再回移重复帧触发钳制。

H.264 普通预测仍需局部证据。下一帧更像重复时，若它正是已有预测位置则保留预测，否则解除相位；不能因为当前帧是预测位置就忽略反证。I 帧邻域禁止依赖 I/P 大小差异建立相位，已建立相位可在时间模式及可比较 P 帧证据支持时继续。普通预测失败立即解锁，后续局部证据可重新捕获。已写出帧不回滚。

当前修改仍处离线验证阶段，未部署：两条 20260923 H.264 全量回放时间轴无回退，左侧已确认错误相位被拦截，短间隔覆盖提高；但光学 CSV 在 I 帧附近仍有约 20ms 残差，尚未达到去稳定漂移后逐帧约 ±3ms 的验收目标。CSV 的 artificial_duplicate 行会复制前帧光学时间，不能作为独立真值；0001 右侧结果还有非共模的秒级累计异常。修正候选数不等于正确识别率，必须继续核对独立光学证据。metadata 固定曝光补偿与每帧 RTC 锚点保持原语义。

局部特征使用原始 encoder PTS，历史稳定条件为前三间隔 20±3 ms；不使用修正后的间隔反向确认。H.265 还结合相对 VCL 大小识别 I 帧附近重复帧，该大小仅为辅助证据，不是固件重复标志。

### H.265 插补相位跟踪

`main_camera_h265_continuity.h` 解析单层、无 B 帧的 SPS/PPS/首个 slice POC，结合 V4L2 sequence 检测编码图像连续性；未知或不连续时解除锁定。锁定后以六帧周期预测，不再逐组要求前三间隔稳定；邻近 ±1 帧强特征允许重新定相位，仍检查正间隔和窗口跨度。I 帧附近结合相对 VCL 大小处理重复候选。跨 IDR 仅有 V4L2 连续性证据，不能证明传感器编码前未漏采。

0008 的独立 LED 有效标定区间左右各 980/980 命中、无额外预测，不覆盖未标定开头和任意场景。0012 的 100 秒 H.265 验证左右修正 998/997 组，各 10 个校时锚点，所有容器 PTS 与预期映射一致且解码正常；这是实现验证，不等同于光学同步精度结论。H.264 保留局部 PTS 识别入口但不使用 H.265 跟踪；0011 原始 encoder 间隔固定 16.666 ms，未命中重复修正规则，独立适配和曝光延迟验证暂缓。

### H.264 新固件适配（2026-09-16）

`2026-09-16-2127` 恢复带长短间隔的原始 H.264 encoder PTS。主摄使用 `main_camera_h264_continuity.h` 解析渐进式 reference I/P 的参数集和 frame_num，结合 V4L2 sequence 作为连续性证据，当前使用独立 H.264 判定策略；普通模式保留局部间隔、VCL 大小和六帧周期辅助，延迟真实帧模式见上文。未知/截断、B帧、非参考帧、隔行、slice groups等不支持模式保守拒绝；frame_num跳号或传输序号不连续解除锁定。IDR边界仅有传输连续性证据，不能证明编码前未丢曝光。旧1728固件固定16.666ms的H.264不满足局部捕获特征，不应据此宣布适配旧固件。XU每10秒校时、逐帧锚点和净-28ms绝对补偿保持；新固件H.264实际曝光偏移需要独立毫秒表标定。

I 帧后重复帧识别补充：当帧序号连续、已锁定周期且完整长短间隔特征成立时，候选不再必须满足 VCL 大小比例。允许预测位置附近一帧重定相位，短间隔可位于后两帧任一处；未知相位、断号和正常等间隔不因此获得修正。0014 已标定范围左右各 982/982 命中，0008 H.265 离线结果保持不变，不能扩展为任意场景全量保证。

Ego 电量由绑定状态探测读取指定 serial 的 `adb shell dumpsys battery`，有效结果每 10 秒刷新，写入 `ego_binding_status.json.battery`；Web 不调用 ADB，仅在连接有效且电量样本不超过 20 秒时展示百分比和充电状态，否则显示电量不可用。查询失败不改变 Ego 连接判定。


### Ego 绑定后持续 USB 校时（2026-09-18）

绑定动作接受后，双手先播放三音上升同步提示，未就绪时循环三短蜂鸣并闪黄灯；同步就绪立即停止蜂鸣、恢复绿色待机灯（其他硬件故障/触觉告警仍按原优先级显示）。声光由非阻塞主循环调度，解绑、绑定失败、同步完成和退出均收口关闭蜂鸣。绑定和同步期间拒绝所有录制启动请求，不创建 episode、不排队自动起录；就绪后需重新按录制。

首次绑定以动作接受到同步就绪 ≤7 秒为目标，满足条件提前就绪；超过目标只记录告警并继续等待，不强制放行。授时服务在每个绑定批次首次打开串口前，使用一次最多 1 秒的 ADB 粗授时（host +20ms，可沿用已有补偿配置），随后只由 USB 串口精同步；粗授时本身不代表就绪。打开串口保留已到达的请求，不使用 PySerial 默认接收清空，避免丢掉 APK 正在等待的请求后触发超时及串口重开退避；分帧解析和 APK RTT/offset 确认仍保留。粗授时与维护/录制锁互斥，录制中绝不执行；失败仍允许串口同步尝试。未就绪时每 100ms 调度检查（ADB 请求耗时另计），就绪后回到 1 秒；只累计 HTTP 查询前已发生的新请求批次，重复查询不累计，失败清零。2 次合格确认比原先 5 次缩短稳定性观察窗口，误差/RTT 门限不变。快照加入 `bound_at_ms`，同 serial 重绑也必须重新确认，不能复用上次就绪状态。

- `ugripper-ego-timesync.service` 随主包安装启动，独立于每条录制；只打开绑定序列号对应的 `18d1:903b` ACM 串口，2 Mbps，使用厂商 `time-sync-server` 的 24/40 字节协议。独立串口线程响应 NTP 请求，打开时立即发心跳、此后仅连续空闲 1 秒才发心跳；ADB HTTP 状态检查不阻塞串口。解绑、重绑和正常停服由串口 owner 在关闭前发送厂商协议的 40-byte `STOP(0xFF)`，避免客户端等待完整交换超时；拔线时尽力发送。重绑、串口断开及服务重启后重新连接并重新累计健康样本。不修改 Ego USB 属性。
- Ego 仅支持 `com.ssnwt.helloxr`，不再识别 egoserver。持续 USB 授时与录制前质量门禁保留，后台状态、广播、数据目录统一使用原包名。
- 起录只读取 `/dev/shm/ugripper/ego_time_sync.json`，不启动同步或通过 ADB 设置时间。要求连续 2 个新交换批次由 APK 确认为 `synced`、offset ≤5 ms、RTT ≤10 ms，服务快照 ≤5秒、有效交换 ≤15秒，且匹配绑定序列号及本次系统启动。当前 APK 不发 RESULT 时以新请求批次加 `/api/status/time_sync` 判定；重复 HTTP 读取不重复累计。无有效持续同步快照时拒绝起录，不回退到录制前 ADB 授时。
- Umi 保留原待机单次 NTP 机制，不额外启用 chrony。成功后写 root 所有的 `/run/ugripper-time-source.json`，记录 boot ID、Realtime/Monotonic 锚点。Ego 授时始终使用背包当前系统时钟，不依赖该凭据或外网；NTP/PTP 调整系统时间后继续向 Ego 授时，检测到单次时钟跳变 ≥100ms 时重建串口会话并重新累计同步质量。Web 的“已与背包同步”仅代表两者对齐，不代表 UTC 已校准。
- Web Ego 卡片显示持续同步状态及偏差/RTT，只读后台快照。每条新版 Ego 数据记录 `ego_sync.json.time_sync_quality` 与 `ego/time_sync_samples.jsonl` 的抽样质量，异常样本保留，不修改既有相机/IMU 时间域或历史时间戳。
- 厂商 APK 当前仍通过 `clock_settime` 校正 CLOCK_REALTIME；持续串口同步不等于平滑 slew。首次大幅校时在录制准入前完成，录制期间仍需检查微小跳变、MCAP/视频单调性及 BOOTTIME 映射漂移；此实现不保证消除起录延迟或提供硬件同步精度。

### 主摄启动锚点与生产日志

主摄从 `VIDIOC_STREAMON` 成功起最多等待 1.5 秒；持续复制码流并立即归还 V4L2 buffer，从最早可解码的干净关键帧缓存连续帧，收到本次 SEI Tset 后按 `本次 Tset + Pframe - 本次 Pset` 回填缓存帧 Unix 时间并按序输出，无需等另一个关键帧或固定睡眠。缓存最多 180 帧/64 MiB 每路；断号、未知解码连续性、encoder PTS 不递增、无效 SEI/buffer 会丢弃候选段并重新等待干净关键帧。使用所有相机 Prepare 前的共同 Unix 时间作为本次相机会话准入下界，拒绝反推后早于该下界的历史帧（此边界晚于 runtime 接收 START，不能补回启动前未采到的画面）；首次新 Tset 后不再接受未知旧锚点。无帧也按总时间超时；超限、超时或停录前仍未就绪明确失败，不输出未确认时间的缓存。首个回填帧作为 PTS=0，原始 SEI 不改、真实 Pframe 间隔保留，正常录制仍沿用逐帧锚点匹配与 10 秒周期校时。`main_startup_gate` 记录等待、缓存/回填/丢弃数量和首帧时间，丢弃仅打印前 3 次。

正常 50Hz 重复帧修正报告 `main_50hz_duplicate` 不输出、不新增统计，修正算法不变。生产日志禁止 TRACE/DEBUG，级别收敛到 I/W/E；主摄连续性诊断每会话最多一次，时间戳修正告警按既有倍增计数限频。Web 成功请求不逐次打印，服务异常使用公共结构化 logger；双目 SDK 原有输出与系统 journal 采集策略不变。

### Fays 无帧恢复状态

双目 ready 同时要求本侧当前 recorder PID 的状态更新、视频与 IMU 时间戳新鲜。待机缺流时给本侧 20 秒总拉流恢复窗口，ready 保持 false、recovery_pending 为 true；上层健康巡检在该窗口内暂缓本侧故障升级，但仍检查其他设备。窗口跨 recorder 重启保持，恢复出帧、设备节点重新映射或 daemon 重启后重新计时。超时仍无帧按现有 ERROR_4 和持续故障确认流程触发 USB restore；已有 restore 进行中、重枚举等待及次数上限仍有效。录制中的断流不使用此待机宽限。

Ego 全局播报语言随 `/etc/environment` 的 `UGRIPPER_LANG` 同步：绑定/重连和语言变更后由状态巡检发送 SET_PROMPT_LANGUAGE，读取应用偏好文件确认；失败每 10 秒重试，成功每 60 秒复核。新旧包名均按实际应用选择，单条 SPEECH_PLAY 继续显式携带当前语言。

Ego 防闪烁同样随 `UGRIPPER_POWER_LINE_FREQUENCY` 同步：50/60 Hz 对应 `persist.sys.static.exp.enable=1/2`，缺省或无效配置按 50 Hz；绑定、重连、参数变更时同步，getprop 读回确认，失败 10 秒重试、成功 60 秒复核，值一致时不重复写入。

Ego 绑定状态探测会同步背包的 `UGRIPPER_LANG` 与 `UGRIPPER_POWER_LINE_FREQUENCY`，分别读回应用偏好与系统属性确认。重连或配置变化立即重试，成功后每 60 秒复核，失败后每 10 秒重试；结果写入绑定状态快照的 `prompt_language` 与 `power_line_frequency`。

## 12. 开发架构与源码入口

本仓主实现位于 `standalone/`，公共适配位于 `src/utils/`，测试位于 `test/`。`build/`、`tmp/`、`.local-deps/` 是产物或环境缓存，不是实现真源。

| 目录 / 文件 | 当前职责 |
| --- | --- |
| `standalone/UgripperRuntime/main.cc`、`record_runtime.cc` | 单实例入口、初始化、supervisor 主循环与装配 |
| `runtime_process.cc`、`runtime_domain.cc` | 子进程管理、录制编排与控制领域逻辑 |
| `runtime_controls.cc`、`runtime_cli.cc` | 录制/按键/FIFO 控制、命令行解析 |
| `runtime_episode.cc`、`runtime_episode_validation.cc`、`runtime_episode_tactile_validation.cc` | episode、metadata、finalize 与停录校验 |
| `runtime_devices.cc`、`runtime_recovery.cc` | 设备状态、HMI I/O、USB 恢复 |
| `runtime_services.cc`、`runtime_support.cc`、`runtime_persistence_support.cc` | audio/stereo/ego 协调、系统与持久化 helper |
| `standalone/CameraRecorder` | 相机应用；`camera_domain.cc` 承载配置、命令拼装与 registry 逻辑 |
| `standalone/FaysStereoRecorder` | Fays SDK 采集、双目编码及 IMU MCAP |
| `standalone/SensorRecorder` | `sensor_protocol` / `sensor_domain`、串口驱动、录制与 `zeroing` |
| `standalone/GripperHmiTool` | protocol / LED effects / driver 类库与工具入口 |
| `standalone/UgripperWebServer` | Web 页面、API 与维护 worker，详见其 README |
| `src/utils` | UGripper 日志/错误码适配及被动 helper；不反向依赖业务模块 |

上表 `runtime_*.cc` 均位于 `standalone/UgripperRuntime/`。Runtime 保留 supervisor 多进程模型，camera/sensor/audio/stereo/ego 按各自生命周期协调；FIFO、状态文件与录制锁是当前通信实现。模块扩展按上述职责归属，避免把领域逻辑集中回 supervisor 壳。

C++ 基线为 C++20。Runtime 公开类型使用 `DA::Ugripper`，内部 helper 使用 `DA::Ugripper::detail`；公开接口沿用 `Initialize/Run/Stop/Get/Is` 风格，`RecordRuntime::Stop()` 只设置退出标志。`DeviceStateManager`、`UsbRecoveryCoordinator` 持有各自状态，episode 通过 DTO 与窄接口接入；录制编排按 episode、filesystem、workers、stereo、ego、feedback、logs 能力分组。

日志后端与单实例能力来自主仓库 `PP_MAIN_ROOT/src/utils`，业务统一链接 `PP::utils`，本仓公共适配目标为 `UGripper::utils`。已有主仓库 `libutils.a` 时直接链接并要求匹配的 Conan 依赖元数据；缺少产物时从同一源码编译兼容桥接。独立配置找不到主仓库时需传 `-DPP_MAIN_ROOT=/path/to/pp-main`。

日志使用 V3 单行外层（Unix 微秒、native TID、等级、调用点），保留业务标签，禁止 ANSI 与重复外层；错误行末尾只保留一个显式 `error_code=UGR-xx-xx`，不按消息关键词猜测错误码。

## 13. Camera YAML 契约

源码配置为 `config/camera_recorder.yaml`，安装位置为 `/opt/ugripper/bin/CameraRecorder/config/camera_recorder.yaml`。当前同时接受两种格式：

| 格式 | 必需结构 | 输出 |
| --- | --- | --- |
| 平铺格式（当前默认配置） | 顶层 `cameras`；每项 `name/device/mode/width/height/fps` | `output_files`，首项为 primary output |
| v1 格式 | 顶层 `CameraRecorder`，其下 `schema_version: 1` 与 `cameras`；每项 `id`、`device`、`record`、`output` | `output.file_name` |

v1 的设备路径、宽高、帧率位于 `device.path/width/height/fps`，流水线位于 `record.pipeline`；可用 `role/side/kind/groups` 描述相机身份。相机列表不能为空、名称不能重复、宽高与帧率必须为正，未知 schema 版本会失败。旧格式仍可用，每进程只输出一次弃用提示。字段解析以 `standalone/CameraRecorder/camera_domain.cc` 为准，配置样例与兼容测试位于 `test/src/camera_recorder/`。

## 14. 构建、打包与部署

顶层 CMake 接入 `src/utils`、`standalone` 与可选测试，要求 `yaml-cpp`、`nlohmann_json` 和各目标开发依赖。`UGRIPPER_ENABLE_MCAP_BUILDER`、`UGRIPPER_ENABLE_PERF_LOG` 默认关闭，`UGRIPPER_ENABLE_STEREO` 默认开启。

在已配置的 ARM 交叉编译容器内，从本仓根目录执行：

```bash
./scripts/build_arm_deb_in_pp_arm_dev.sh --no-clean
# 只组包，复用已有且构建变体一致的 ARM 产物：
./scripts/build_arm_deb_in_pp_arm_dev.sh --quick
```

脚本使用 `cmake/arm-linux-toolchain.cmake`，默认产物目录 `build/arm_container_release`；不传 `--no-clean` 时完整构建会先清空该目录。它不会创建或进入容器。环境准备见根目录 README、`docs/docker-arm-container-env-request.md` 和 `scripts/setup_arm_build_env.sh`。

主包入口 `build_deb.sh` 构建/校验七个目标：`CameraRecorder`、`main_camera_xu_tool`、`SensorRecorder`、`zeroing`、`GripperHmiTool`、`UgripperRuntime`、`fays_record_example`。`-q` 跳过编译，使用 `PACKAGED_BUILD_DIR`；直接运行标准模式会本机编译，不能在 x86 主机上用它代替 ARM 交叉编译。包名为 `ugripper_<version>_arm64.deb`，版本取脚本的 `BASE_VERSION/VERSION_SUFFIX/VERSION`，双目变体由构建参数决定；文档不维护另一份版本号。

主包按 `bin/<AppName>/` 安装二进制与资源，同时交付启动/维护脚本、Web、配置、udev 和 `.venv`，并检查核心二进制与 Python 的 ARM 架构。Python 版本与依赖以 `pyproject.toml` 为准；默认归档 URL 由 `build_deb.sh` 的 `PACKAGED_VENV_URL` 定义。本地 ARM 环境替代方式：

```bash
PACKAGED_VENV_URL='' PACKAGED_VENV_SOURCE=/path/to/arm64/.venv \
  ./scripts/build_arm_deb_in_pp_arm_dev.sh --quick
```

`./scripts/build_runtime_venv.sh` 用于构建运行环境，支持 `VENV_VERSION_TAG`。独立 updater 由 `usb_updater_build.sh` 生成 `das-usb-updater` 包，USB 自动升级和开机恢复 unit 归该包所有。

主包 unit 包括 `ugripper.service`、`ugripper-calibration.service`、`ugripper-network-monitor.service`、`umi-shutdown-trigger.service/.path`、`ugripper-ntp-sync.service`、`ugripper-ego-timesync.service`、`ugripper-maintenance.service` 和 `ugripper-webserver.service`；校准服务按需触发。安装清单以 `build_deb.sh` 为准，启停策略以 `pack_script/postinst` 为准。udev 规则为 `99-fixed-usb-map.rules` 与 `99-serial.rules`；运行目录权限由 `config/ugripper-tmpfiles.conf` 管理。

## 15. 测试与维护入口

| 层级 | 入口 | 证明范围 |
| --- | --- | --- |
| host 单测 | `test/src/`、GoogleTest/CTest 的 `host-only` 标签 | 无硬件逻辑、边界条件与合成输入 |
| 离线数据校验 | `test/scripts/validate_episode_data.sh`、`check_video_windows.py`、`check_sensor_mcap` | episode 结构、视频与 MCAP 时序/覆盖率 |
| 板端链路 | `test/scripts/board_sensor_smoke.sh`、`board_camera_stress.sh`、`board_service_integration_check.sh`、`board_service_episode_loop_check.sh` | 真实设备、服务与录制链路 |
| 人工交互 | `test/scripts/board_gripper_hmi_active_check.sh` | 按键、灯效、蜂鸣与人工刺激 |
| 压力与长录 | `test/scripts/` 中对应 soak/stress 脚本 | 指定设备、负载和时长下的稳定性 |

host 环境安装开发依赖、GoogleTest 与 Python 后，可在仓库根目录配置独立测试目录：

```bash
cmake -S . -B build/host-tests -DBUILD_TESTING=ON -DPP_MAIN_ROOT=/path/to/pp-main
cmake --build build/host-tests --parallel
ctest --test-dir build/host-tests -L host-only --output-on-failure
```

详细脚本参数见 `test/README.md`。板端结果存放 `tmp/board_results/<run>/`，应记录源码/包版本、设备、命令、结论与覆盖边界；历史通过不代表当前版本已验证，host 单测也不替代板端或曝光真值验证。

本文只维护当前架构、行为与使用方法。有效待办集中到 `docs/todo_list.md`，对外功能变更记录于 `docs/CHANGELOG.md`；协议、数据说明与用户手册保留为专题文档。

### 主摄重复 PTS 日志口径

固件插帧重复原始时间属于已知情况，`main_timestamp_clamp` 不再为此逐次打印 WARN。判定真实回退时比较相邻输入帧时间，不能拿已加 1us 的输出 PTS 判断后续重复帧为回退；输出严格单调策略和会话钳制统计保持不变。重复串异常是否来自固件需查看原始 V4L2 码流 SEI，不能仅凭钳制后的 MKV 推断丢帧。

- 主摄原始 SEI encoder PTS 连续相同达到 3 帧（原帧加至少 2 个重复帧）时，通过公共 `DM_LOG_WARN` 输出标准 V3 warning `main_duplicate_pts_run`，包含相机、编码、PTS、V4L2 sequence；同一重复串仅在第 3 帧告警一次。正常两帧同 PTS 静默。检查位于时间映射和钳制之前，无效帧重置连续计数。

左右主摄补偿采用 20260928_0030 光学标定：扣除共同线性漂移后，旧补偿下左侧误差 +3.528ms、右侧 -0.440ms，统一设为 +7000us；预计固定残差左 +0.128ms、右 -0.240ms。0026 标定错误，排除。仅修改绝对起点，不平滑帧时间、不补偿共模漂移；其他主摄 fallback 仍为 +8600us，未经独立标定。需用 0031 独立复核。

### 双目录制掉帧门限

每侧按本条录制实际送入编码器的相邻原始 Fays 时间戳、名义帧率估算缺失帧数（半帧周期容差），跨会话和首帧不计间隔；SDK 暂无新帧轮询次数不计为掉帧。每侧单次连续缺失超过 3 帧复用 `UGR-22-04`，日志给出相机、累计 `dropped_frames`、本次 `gap_dropped_frames` 和帧序号。runtime 首次发现超限后用单调时钟等待 1 秒再错误停录，倒计时不阻塞控制循环、不因后续丢帧延长；用户主动停录和其他严重故障仍可优先结束。

双目丢帧状态仅在 `/dev/shm/ugripper_stereo_loss_*` 进程间传递，不写入 episode；录制端、运行时和停录校验共用路径计算。单侧单次连续缺失超过 3 帧时，通过现有 `quality_check_status=fail` 与 `quality_check_err_type=stereo_frame_loss` 上报；掉帧数只保存在共享内存状态和日志中，不新增 metadata 字段。`stereo_frame_loss` 映射 `UGR-22-04`，原有 `frame_loss` 语义保持。统计基于保留帧时间间隔，包含可从间隔观察到的 SDK/应用队列缺帧；不是硬件序列号精确计数，首帧之前及末帧之后缺帧由既有无帧/断流检测负责。

双目停录门限只比较一次相邻保留帧间隔对应的连续缺失数，不将多个间隔累加。共享内存状态保存 `max_consecutive_dropped_frames`，保证一次超限不会被后续小间隔覆盖；累计 `dropped_frames` 仅用于日志统计。两次各缺 2 帧不触发停录，单次缺 4 帧触发；metadata 不增加字段。

### 停录 QC 视频探测一致性

停录后 metadata 与 QC 共享同一组内存视频探测结果（包含失败原因），下一条/重录准备时清空；不复用上一条 episode 的结果。仍先原子写出 metadata 再执行原有 metadata 完整性检查；失败写回只更新质量结论，不重新运行 ffprobe。metadata 协议及字段不变，写入失败进入 `finalize_error`，不能报告采集成功。

ffprobe 使用 JSON 输出，stdout/stderr 分离并在进程运行时持续排空；每路最多尝试两次，每次超时 3 秒，输出保留上限每管道 1 MiB，stdout 超限判失败。失败/重试日志包含视频路径、次数、耗时、退出码及 stderr。正常每路仅探测一次，各路并行；有效 JSON 不会因 stderr 诊断文字而发生位置解析错误。触觉灰度抓帧共用的进程捕获也避免 stderr 污染像素数据。

原有 QC 全部保留：必需文件非空，主摄在位/SN/标定缓存，ffprobe 可用及视频可读，metadata 视频条目及时间字段，有效跨度最小 0.2 秒，各路跨度差最大 5 秒，Fays MCAP topic/消息/有效跨度与 IMU 尾部、encoder 同侧最晚视频尾部覆盖（最大落后 1 秒），双目单次连续掉帧超过 3 帧，触觉异常历史。录制进程/硬件异常、Ego finalize/拉取、stereo finalize/merge、目录 rename 等既有失败判定和优先级不变；既有失败短路语义也不变。

正式停录触觉检测导出同次 C++ 判定的 mask（普通残差为过滤后的实心核心，结构损坏为不匹配块）；不改变损坏阈值或三次历史规则。处于告警状态且本次仍损坏的短期/长期比较按 episode 汇成一组，原子写入 tactile_state/latest_alarm.json，后续告警组覆盖旧组；无新告警保留历史。Web 概览仅在当前黄灯告警且 SN 匹配时显示 SN、位置、基线类型、episode/时间及盖板底图叠加红色半透明 mask 的 PNG；正常或断连时隐藏，底层以只读接口提供图像，固定尺寸/白名单校验并绑定组 ID，避免覆盖期间混看不同组。仅保留一个诊断 JSON，不增加 metadata 字段；最多 8 张 160×120 mask，不累积历史文件。

- Ego 起录前查询真实录制状态；残留录制先 STOP 并确认 idle 与旧 episode 完成 rename，保留旧数据后重新起录。恢复等待 20 秒，Runtime 恢复启动总窗口 60 秒，失败阻止本次起录；不新增 metadata 字段。

### STOP 终态与绿灯前写入边界

Ego STOP 每 50–150ms 查询目录，录制中的增量周期不变。文件大小一次 ADB 批量读取；尾部补拉后重新批量核对，全部一致时不重复补拉，不符仍走有限恢复。MP4 头刷新、修复检查和删除前复核保留。STOP Python 完成拉取后保持存活，收到主机数据刷盘确认才在同一进程清理远端；清理完成/退出后才越过收尾屏障，主机退出或预算耗尽则保留远端。

发现同名 `-invalid` 时核对 metadata 的设备序列号、episode 名、UUID、起始时间与本次 START；本地已有 UUID 时要求一致，不把旧失败目录当成本次结果。确认后立即记录 `invalid / ego_pull_incomplete` 与 App 的 `quality_check_err_type`，有界补拉失败诊断数据并保留远端，不再等待普通目录直到 60 秒。正常完成目录优先；仅当临时目录和正常目录均不存在时检查同名 `-invalid`。未发布完整 metadata 或身份不符的失败候选限频记录诊断后继续观察。该终态纳入既有绑定 Ego 的 ERROR_5 门禁。

本地关闭后的预校验及触觉分析与 Ego 拉取并行；最终完整校验继续执行。触觉结果在最终 metadata 存在后原子发布并前台刷盘，不再在 Ready 后启动 episode metadata 的后台重写。日志早期快照和末尾补增量全部完成、最终前台 `sync -f` 返回后才允许 Ready。内核不可中断写盘仍不得通过超时提前放行绿灯。

触觉停录告警日志按基线分别输出：`scope=reference` 为短期、`scope=persistent` 为长期；`event=triggered/active/cleared` 表示新触发、持续、解除，并附 camera、serial、episode、本次 damaged/clean、core_px 和 structural；完整算法指标保留在诊断结果中，不展开到告警日志。长期锁存告警本次未比较时标记 `detail=not_evaluated`，不冒用短期指标；三次历史、黄灯及语音判定保持不变。


## 外部应用设备 API

- HTTPS 沿用 WebUI 地址和维护密码；只读 `/api/v1/versions` 支持任意已安装 deb 包名及 metadata 全部版本字段，`/api/v1/devices` 支持完整硬件清单和指定设备，`/api/v1/recording` 提供起录/录制/收尾状态。版本与设备信息复用现有 firmware/Ego/Fays 缓存及 dpkg 数据库，不占用设备端口、不新增版本快照文件，不改变 metadata 格式。
- 外部应用把完整包原子放入 `/var/lib/ugripper-web/upgrade-packages/`，调用 `POST /api/v1/upgrades` 即强制升级，无二次确认；仍检查包合法性、设备匹配、录制/维护互斥及至少 30% 的有效背包电量。Web 与外部共用维护 worker，结果按任务 ID 保存在 `upgrade-results/`。
- 3588 固件写入回读成功后返回 `awaiting_reboot / success=true / boot_verified=false`，不自动重启；外部或 Web 单独发送任务 reboot 指令。重启前清理临时升级目录，实际重启后再次清理并校验 boot 分区；任务记录和恢复标记保留在临时目录之外。服务重启保持等待状态，不自动重刷。
- 参数、字段、状态与调用示例见 [设备 API 汇总](../device-api.md)。

### Ego 长录终态校验与不完整数据报错

Ego metadata 包含随录制时长增加的授时映射段，终态身份校验允许最大16 MiB，按既有1 MiB传输块读取；设备SN、episode、UUID及START时间校验保持不变。正常完成目录沿用最终文件补拉和大小核对，不因同名旧 `-invalid` 增加身份校验；仅失败终态使用上述有界 metadata 身份检查。

已尝试Ego录制且收尾返回 ego_pull_incomplete 时，无论当前绑定快照是否存在，均使episode失败、metadata记录对应错误并进入ERROR_5；不再把该错误当作可选设备的非致命失败。未启用/未发现可选Ego且没有录制的原有成功分支保留。远端清理失败仍不覆盖已经核实完整的拉取结果。

### USB IRQ 启动亲和性兜底

UGripper的root ExecStartPre运行config/usb_irq_affinity.py，按在线CPU容量识别RK3588大核，动态发现USB控制器IRQ，按供应商usb-irq.sh的USB总线顺序去重并均分到大核，仅调整默认亲和性；不写死IRQ号。已有仅大核策略及非默认显式策略保留，不重复覆盖供应商配置。未使用的PCI MSI向量不调整；非RK3588或无大小核时跳过。

`UGRIPPER_USB_IRQ_BIG_CORES=false` 可在systemd drop-in关闭软件兜底，后续固件接管时可使用。运行中的irqbalance会导致跳过并明确记录冲突，软件不禁用全局irqbalance服务；测试固件需继续保持其关闭，或由系统管理员配置IRQ排除策略。只在服务启动运行，不后台循环抢占亲和性；固件后续变更不被软件改回。启动时尚未发现的控制器需下次服务启动重新发现。

状态保存在root专用/run/ugripper-usb-irq-affinity，停止/启动失败只恢复本软件确实更改且IRQ身份、当前值、boot_id仍匹配的项目；外部修改保留。不修改内核cmdline、全局workqueue、USB电源或CPU深睡设置。启动/恢复失败写journal；IRQ优化失败不阻止应用启动。

### 两核触觉分配与采集丢失诊断

`fays_split` 下，触觉 FFmpeg 在 fork 后 exec 前固定到单个大核：left_l/right_l→CPU6，left_r/right_r→CPU7；解码/编码工作线程继承亲和性，不使用后台 taskset 扫描。其他角色及 USB IRQ/workqueue 策略不变，profile off/不支持的拓扑仍保留继承设置。

本机采集流使用 O(1) 状态统计。录制中不再以累计丢失率停录：主摄、双目单次连续缺失超过3帧触发原有故障链路，触觉单次连续缺失达到8帧触发停录；双目保留现有1秒延迟停录。主摄无效帧与相邻序号缺口合并计连续缺失，触觉按相邻输出PTS与名义周期估算；首帧之前不计，多个独立缺口不相加为连续缺失。编码器和Fays IMU保留既有断流、队列溢出、时钟异常保护。

停录排空后按每路本次episode最终 `missing/(received+missing)` 严格大于2%判失败，不设1秒豁免。编码器尾部排空样本纳入统计；Fays视频在同步STOP屏障内结算，IMU在MCAP队列排空后结算。最终结果仅经 `/dev/shm/ugripper_stereo_loss_*` 临时状态传给最终QC，起录写入pending覆盖旧结果，未完成或损坏的结果不能通过校验；无该状态的历史数据沿用原有校验。不新增episode文件或metadata字段。日志每路仅首次连续缺失超限和最终汇总各一条，不输出逐帧日志，也不再输出累计超过10帧的告警。

口径：主摄使用启动门控通过后的 V4L2 sequence 缺失及无效帧，不把 PTS 的1µs/40ms节律当确认丢帧；仍不能检测相机编码前漏采或仅发生在固件内部的缺帧。触觉按输出PTS跨度、实际帧数与配置名义周期估算（不把一长一短的抖动分别累加为丢帧）；双目使用保留帧的设备时间，覆盖采集至消费链路的总缺失估计、不与队列丢弃重复相加；Fays IMU按SDK标定的 `imu.update_rate` 和原始设备时间估算，名义频率不可用时报监测错误；编码器在平滑前按原始主机接收时间的累计应有数与实际有效数比较，避免把同一批次成批交付逐个当丢失。后几类是估计值，不能区分硬件漏采和传输/调度迟到，批次补齐后当前缺口可下降，累计比例只在收尾判断。写盘失败、时间回退、完全断流仍由原有保护处理。

Ego在远端采集，当前主机没有逐流实时序号/累计计数接口：仅已有连接、传输和停录完整性校验；Ego内部视频/IMU漏采实时检测尚未覆盖，需要供应商提供遥测，不能标记为零丢帧。当前生产本机模态为主摄、触觉、双目、Fays IMU和编码器；未启用设备不参与统计。


### 主摄逐帧诊断（默认关闭）

定向排查主摄补帧/时间戳与传输丢帧时，可在 CMake 配置中开启 `-DUGRIPPER_MAIN_CAMERA_FRAME_TRACE=ON`。`CameraRecorder` 在 episode 中缓冲写入 `left_cam_main.frame_trace.csv` / `right_cam_main.frame_trace.csv`，不向系统日志逐帧输出。capture 行记录 V4L2 sequence、主机 steady 时间、原始 SEI encoder PTS/XU 时间、有效性、编码连续性及 H.265 POC；output 行记录应用 ordinal、原始/修正后的 Unix 时间及最终 PTS。`xu_or_unix_us` 列在 capture 行为 XU 时间，在 output 行为修正前 Unix 时间。H.264 的 POC 列为 -1，不表示 H.264 解析失败。

测试需同时保留原始视频、日志、构建标识，停录后检查 sequence/POC 连续性及完整软件解码；最终 PTS 的 40ms gap 不能单独作为确认丢帧。Debug 构建和逐帧诊断会改变负载，本轮未复现不能证明长期零丢帧。常规发布保持此选项 OFF，测试结束恢复原程序。

### 编码器 CPU3 正式分配

`fays_split` 服务启动时，在 RK3588 原始允许 CPU0–7、CPU3 在线且列于内核 `isolated` 的条件下，bootstrap 设置内部 `UGRIPPER_ENCODER_CPU3_ACTIVE=1`。SensorRecorder 主线程及后续创建的左右独立请求、接收解析、写盘线程均为 CPU3；请求线程再次配置亲和性时也保持 CPU3。服务辅助进程改为 CPU0–2，主摄、双目和触觉沿用现有分配。日志线程在主线程改绑之前创建，继承辅助核；CPU3 隔离不等于排除所有内核/IRQ工作。

条件不满足时保留原 `fays_split` 分配，不修改固件、内核启动参数或全局 workqueue。可通过服务 drop-in 设置 `UGRIPPER_ENCODER_CPU3=false` 并重启回退；`UGRIPPER_CPU_PROFILE=off` 关闭整个绑核配置。内部 ACTIVE 标志由 bootstrap 清除并重算，不能作为用户配置项。

### 采集普通调度优先级

`ugripper.service` 使用 `LimitNICE=30` 允许子进程将 nice 提高至 -10，设置 `UGRIPPER_ACQUISITION_NICE=-10`，不设置服务整体 Nice 或实时调度策略。CameraRecorder、FaysStereoRecorder、SensorRecorder 在主线程启动采集工作之前应用设置，随后创建的请求、接收、写盘线程及 FFmpeg 子进程继承优先级；已创建的日志线程保留继承值。Runtime、Web、上传等辅助进程不主动提高优先级。

仅普通 `SCHED_OTHER` 调度应用该设置，保留管理员指定的其他调度策略及更高优先级。支持 -10..0；未设置或空值不操作，非法值及权限不足明确告警。服务 drop-in 设置 `UGRIPPER_ACQUISITION_NICE=0` 后重启可回退。无需 CAP_SYS_NICE，不修改全局资源限制；nice 只影响CPU竞争，不能保证消除I/O、USB和内存带宽竞争。本次仅本地检查，尚未在更换夹爪后的240部署验证。
