# Changelog

## Unreleased

- 隔离 CPU3 的 RK3588 固件正式启用编码器 CPU3 分配，辅助服务线程避开 CPU3，保留旧固件回退和关闭选项。

- 将 fays_split 的四路触觉编码进程明确分配到 CPU6/7，工作线程继承，减少隔离核下负载集中。
- 本机采集流增加轻量丢失计数与一次性阈值告警：丢失率>2%报错，相机累计>10帧warning；区分序号证据与时间估算，保留每路收尾诊断，Ego远端实时漏采检测明确不在当前覆盖内。

- 丢帧诊断不再向 episode 写入 frame_loss/stream_health JSON；双目跨进程状态迁至共享内存，保留连续丢帧延迟停录与校验，各路收尾统计仅写日志。

- 服务启动时为RK3588默认USB IRQ亲和性提供大核分配兜底，保留固件显式策略，支持关闭；停止时按归属恢复，检测irqbalance冲突而不修改其全局配置。

- 3588 设备固件升级改为校验并执行完整发布包内 update.sh，支持 WebUI 上传归档或 PC 解压目录；成功后单独确认重启。补充缓存 /home/user/version.info 至 Web/API 与 metadata 的 rk3588_firmware_version，数据版本保持 3.3，原有字段格式不变（7b2ec87 同功能补充）。

- 系统 syslog 改用本机 `/var/log/ugripper-syslog/syslog.log`，启动前按挂载顺序创建目录并固定读写权限，消除 `/tmp` 后挂载遮蔽目录的问题；保留原格式、容量轮转及事件增量复制至数据盘，跨重启不重复导出已有源文件；配置提交成功后删除旧 `/tmp/logbuf`，本地历史轮转文件最多保留 9 个。

- WebUI 与外部 API 的所有软件、主摄、夹爪和 3588 固件升级统一要求背包电量至少 30%；任务接收及安装/烧录前复核，低电量或电量不可用拒绝，强制升级与充电不豁免。

- 新增外部应用版本、全部硬件连接和录制状态查询，以及放包后强制升级/按任务查询结果接口；复用现有缓存及 Web 升级链路。3588 固件写入校验与确认重启分离，重启清理临时包并保留结果与恢复记录，metadata 格式不变。

- Fays SDK 升级到 v3.8.4，左右 `imu_v4l2_buffer_count` 配置为 64，扩大 IMU 接收缓冲以优化高负载或调度延迟下的缓冲溢出问题，双目图像 buffer 不变。

- 触觉告警日志分别标明短期 reference / 长期 persistent 及触发、持续、解除状态，沿用 V3 日志格式和既有告警判定。

- 识别并核对 Ego `-invalid` 录制终态，保留 App 失败原因和远端副本，避免等待普通目录至 60 秒；触觉 metadata 更新移至绿灯前发布及刷盘，分析与 Ego 收尾并行。

- 停录并行保存早期系统日志快照及预校验本地文件；事件型常驻日志 worker 减少解释器启动，最后补齐增量并前台刷盘后回绿。Ego 加快 STOP 轮询、批量查询文件大小，保留最终复核；同一 STOP 子进程在主机刷盘确认后清理远端，最终完整校验不省略。

- Ego 起录提前预热 Python/设备探测，缓存绑定设备身份、App 信息与数据路径；正常起录仅做新鲜连接/APK 检查，编码未变时省略重复设置，保持旧会话恢复、授时门禁及增量/STOP 的进程与刷盘边界。

- 系统日志固定 RAM 缓冲，仅事件导出 syslog 增量文本快照；统一 system_journal/rsyslog 路径、单卷 100MiB/总量 500MiB/30 天留存、原子清单和前台 sync -f，完成后才回绿；停止周期 U 盘日志写入，不导出原生 journal、不在停录全量重写/压缩，Web 下载包含已提交文本片段。系统配置预校验、逐文件原子替换，安装/卸载失败回滚并支持中断恢复，保留初装前配置。

- 2.1.15：Web 主摄设备版本读取失败时支持弹窗确认后继续强制升级；支持升级前及刷写后回读失败，保留镜像与硬件检查，取消/超时终止并记录未验证状态。

- Ego 增量传输改为无 PTY 的有界分块读取；卡住先正常退出、超时再强杀，录制中续传同一 episode，停录由收尾接管补齐，保持同步落盘后才恢复绿灯。

- 修复增量打包时 root Web 维护目录嵌套、旧模块未覆盖的问题，production/bin 改为原位同步并清除旧文件。

- 修复 Web 主摄升级的设备读取失败被旧命令重试覆盖：镜像/设备版本分开读取，仅参数格式错误回退；缺摄错误明确显示设备路径并保留原始输出。

- metadata 3.3 固定新增 12 个版本字符串，主摄版本取 USB iSerial；补充 Ego 绑定状态/SN、起录版本门禁和 ego/metadata.json 的 UMI episode name/UUID 关联，保持其余字段格式，Web 与校验范本同步。

- HMI 所有串口帧统一增加发送队列排空检查与25ms静默（另计8N1线速时间），覆盖蜂鸣/RGB紧邻发送；队列等待有界，超时按I/O故障处理。

- HMI 修复蜂鸣后静默期持锁忙转，命令按优先级与实际发送期限调度，重复保活错峰；保留静默/重试规则，接收检查等待最多 20ms。

- Fays 左右健康解析改为常驻 helper，减少周期解释器启动；快照过期拒绝、异常自动恢复，并区分解析器不可用与相机故障。

- 停用闲置 USB 耳机播放/按键扫描，避免周期启动 udevadm/pactl；Ego 语音保持独立。
- 编码器空读轮询改为 250µs，保留有数据时连续读取和停止检查。

- 编码器保留 1kHz 请求计划与迟到补发，每次写入后至少间隔 500µs，避免过度密集补发触发持续无回复；周期请求使用非阻塞写，处理短写和超时，绕开 TTY poll 接收队列等待；均衡 CPU 配置下仅请求线程共享 CPU4–5，避开辅助脚本及视频编解码竞争。

## V2.1.14 - 2026-10-12

- 安装时直接重建损坏 Web 配置及证书/私钥，不备份；有效文件保留，配置原子写入。夹爪版本读取失败后持续限频重试。
- 修复 NTP 校时后的 Fays 映射慢追：待机快速重建，未收敛禁止起录；录制中确认的大幅跳时按故障停录。
- 停录并行发起 Ego STOP 与本地 recorder 收尾；增量写入交接和 Ego 收尾共用 60 秒预算，失败保留未验收远端数据。
- Ego MP4 使用有界检查与修复，encoder 尾部只查末尾最多 4 个 MCAP chunk；避免长录全量扫描。
- 系统日志同步增量快照并限制容量；数据和日志落盘完成后才 Ready，失败不亮绿灯，增加停录阶段耗时日志。
- 增加停录、NTP、红灯、Ego 起停/超时等测试清单，记录 Ego App metadata 固件版本查询重试本次额外约 11.4 秒的等待来源。

## V2.1.13 - 2026-09-29

- Ego 起录前查询真实录制状态；残留录制先 STOP 并确认 idle 与旧 episode 完成 rename，保留旧数据后重新起录。恢复等待 20 秒，Runtime 恢复启动总窗口 60 秒，失败阻止本次起录；不新增 metadata 字段。

- Fays IMU 改为回调与有界队列，隔离 episode 启停数据；时钟映射抑制迟到跳变、支持持续偏移更新和断流后恢复，保护双目时间戳单调。
- 默认启用 RK3588 均衡 CPU 分工：控制/日志/辅助用小核，Fays 共享两个大核，其他视频用另两个大核，编码器保持多核；可配置关闭。

- 蜂鸣关闭状态增加每秒 OFF 保活，保活与状态查询间隔至少 25ms，避免关闭帧丢失后持续鸣响；关闭回读允许固件保留频率值，以占空比为零判定静音。
- Ego 绑定增加一次粗授时并加快连续同步确认，补齐串口关闭 STOP 通知并仅在空闲时发心跳，目标 7 秒内就绪、达标提前放行；等待期间三短音配合黄灯闪烁，就绪恢复绿灯，同步前禁止录制，同序列号重绑不复用旧同步状态。

- 修复停录视频探测与 metadata 结论不一致，保留全部 QC，增加有限重试及具体错误日志。
- Web 仅在黄灯告警且传感器 SN 匹配时展示盖板底图与红色 mask 叠加图，滚动保留最新一组告警。
- Web 新增 RK3588 boot 固件升级：校验镜像与目标分区、电量至少 20%，写入后重启并回读确认；维护操作增加逐次密码校验。
- Ego 新增持续 USB 授时、重连恢复与偏差/RTT 显示，同步语言及抗工频配置；使用 `com.ssnwt.helloxr`。
- 双目就绪检查要求视频/IMU 数据新鲜，待机恢复窗口为 20 秒；单次连续丢帧超过 3 帧时报错，延后 1 秒停录，现有 `quality_check_err_type` 写入 `stereo_frame_loss`。
- 适配新主摄固件：H.264/H.265 均通过与前一帧相同的原始 PTS 识别重复帧，输出 PTS 递增 1µs；正常重复静默，连续至少 3 帧同原始 PTS 输出标准 warning。
- 左右主摄时间补偿统一为 +7ms，四路触觉统一为 -2ms。
- 修复 Web 毫秒表测试数据目录选择和日志刷新；补充数据时间对齐说明及 Web 手册。
- 保留 encoder 起录双侧样本确认、3 秒断流停录及末帧覆盖校验；当前 `V2.1.12` tag 已回补相同修复，本版本无额外 encoder 算法变化。

## v2.1.12 - 2026-09-20

> 变更截至 tag `V2.1.12`（含）。

- 新增免登录 HTTPS 设备管理页，支持设备状态、日志与数据下载、软件/固件升级、参数配置和编码器标零；录制、下载与维护互斥。
- Web 展示左右设备 SN、固件版本、背包/Ego 电量及 Ego 连接状态，支持相机抓图、编码器实时读数和触觉盖板检测；编码器角度与录制数据统一为有符号值。
- Web 新增自动压测与毫秒表同步测试，支持进度查看、取消和结果保存；优化中英文显示与数据分页。
- Ego 新增低电量、数据收尾、存储不足、校验失败和触觉异常语音提醒，并按硬件版本配置输入电流。
- 背包电量改为读取蓝牙服务快照，避免串口争用；Web 自动刷新并提示快照异常。
- 主摄 H.264/H.265 统一 RTC/SEI 时间映射与周期校时，修复校时过渡帧误判和 50 Hz 重复帧时间修正；新增 50/60 Hz 抗工频配置。
- 主摄起录增加 1.5 秒时间锚点等待与有效帧回填，减少空文件和起录画面缺失；修复固件升级后 iSerial 变化导致的误报。
- 双目采集与编码分离，灰度数据改走 NV12 硬件编码；RK3588 大核 workqueue 配置缓解接收延迟。
- 修复双目停录视频与 MCAP 收尾竞态及服务重启残留 FIFO，优化初始化等待与断流重试。
- 触觉视频保留真实采集时间与帧间隔，更新相机时间补偿；优化触觉损坏检测，减少纹理漂移和曝光变化误报，提供独立检测工具。
- metadata 升级至 3.2，新增开机时间与触觉损坏 SN 列表；标定格式升级至 3.1，支持主摄外参及独立标定状态，离线校验器兼容历史格式。
- 收敛运行日志格式与重复输出，停录保存系统日志；新增离线日志查看、错误码筛选和故障参考。
- UgripperRuntime 完成领域拆分，统一公共接口和日志工具；毫秒表联动改为常驻 GPIO 控制并记录触发时间。
- 新增 ARM 增量构建部署入口，默认主包版本调整到 `2.1.12`。

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
> 以下早期版本的发布边界按 `build_deb.sh` / `scripts/build_arm_deb_in_pp_arm_dev.sh` 中 `BASE_VERSION` 的提交记录推定。

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
