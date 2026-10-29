# UGripper Web 设备维护

独立 HTTPS 页面服务与本地维护 worker，源代码位于此 standalone 目录。

- 页面：`https://192.168.2.240:8080/`。独立 HTTPS API：`192.168.2.240:8081` 和 `127.0.0.1:8081`，只提供 `/api/v1/`，不监听无线 IP 或通配地址。示例见 [设备 API](../../docs/device-api.md)。
- `config.json` 的 `bind` 必须为 `192.168.2.240`，`port` 仅控制 WebUI 且不得为 8081。三个监听器共用 8 请求并发额度，全部绑定成功才开始服务；绑定失败检查网口地址和端口占用。目的地址绑定不替代防火墙入接口限制。
- 接上网线即可直接进入页面，无账号或登录步骤。首次安装只生成证书和 `/etc/ugripper-web/config.json`；覆盖安装保留配置文件和证书（启动时仍按上述地址限制校验），清理旧登录密码哈希及 `initial-password` 文件。配置缺失、空白、非法 JSON 或非对象时直接重建默认配置；证书/私钥缺失、为空、损坏或不匹配时成对重建，不备份。配置通过临时文件同步落盘后原子替换。
- 页面访问仍免登录；上传软件/固件、执行升级、修改设备参数、编码器标零必须逐次输入维护密码，由设备管理员保管。服务端在读取上传或调用 worker 前校验 `X-UGripper-Maintenance-Password`，错误/缺失返回 403；浏览器不保存密码，确认/取消后清空输入，不随任务写入 worker 状态。此共享口令仅用于防止随意修改，不提供个人账号或权限分级。
- HTTPS 使用设备自签证书，首次访问可能需要浏览器接受证书；生产交付可替换为受客户电脑信任的证书。指纹可用 `openssl x509 -in /etc/ugripper-web/server.crt -noout -fingerprint -sha256` 核对。
- HTTPS 服务以 ubuntu 运行；独立 root worker 使用 `/usr/local/lib/ugripper-web/` 的 root 专属代码与工具副本及系统 Python（包内提供 pyserial），仅监听本地 Unix socket，不提供 shell/任意命令接口。
- 所有连接设备以太网的用户均可查看状态、下载数据/日志、抓图、检查盖板、读取编码器及操作自动测试；软件/固件升级、设备参数保存和编码器标零额外要求维护密码；触觉基线重置仅弹框确认。写请求由页面自动携带公开固定头 `X-UGripper-Request: 1`，校验 Origin 且不开放 CORS，以阻止跨站页面触发操作；不使用登录 Cookie 或会话。升级确认、治具确认与互斥锁始终生效。
- 背包电量直接读取蓝牙 BMS 快照，未接 HMI 或 Runtime 未启动也可显示；数据缺失、非法或超过 20 秒显示不可用，不新增串口采集或周期日志。
- 状态仅读取设备节点、系统统计和 Fays 状态快照；“已识别”不等于固件通信健康。双目帧的新鲜度使用最近帧时间，缺失证据不会显示为正常出流。

- Ego 卡片与 HWS 共用 `ego_status.py`，显示绑定、连接和绑定序列号；仅读取 `/dev/shm/ugripper/ego_binding*.json`。连接快照须匹配绑定序列号且不超过 15 秒，过期/异常显示未知；未绑定不检测连接，USB 存储模式不代表 ADB 已连接。

## 互斥与任务

`/run/ugripper/maintenance.lock` 为永不删除的 flock inode。录制从创建 episode 前持有至收尾结束；数据下载从打开文件至响应结束持有；维护从接受任务至设备恢复和文件清理结束持有。旧 `/tmp/umi_recording.lock` 状态语义保留。USB updater 同时使用该锁和自己的实例锁。

录制中拒绝上传/升级/标零/下载；下载中拒绝开始录制。状态查看不锁定设备。断线、进程退出释放文件锁；刷写任务在客户端断线后继续完成。重启后的未完成任务标记失败，不自动重试烧录。自更新由独立 worker 执行，维护锁持续覆盖 dpkg 与 postinst，任务完成后才重启 worker。

上传体积严格小于 200 MiB，流式落盘；上传文件使用随机编号，worker 复制到 root 私有目录后校验。结束后清除固件/deb，保留任务摘要和日志。每台设备只保留一份待提交上传；服务重启清除未提交文件。

- 软件白名单与 `../DASUsbUpdater` 一致：das-usb-updater、ugripper、bluetooth-gatt-server、databot-device-joint、device-ota-mender。按 deb 内部 Package 字段校验，接受 arm64/all。通过 `dpkg --no-act --install` 预检，再使用 `dpkg --install` 覆盖安装，不在线下载依赖。
- 主摄：单份 IMG 顺序升级左右主摄；升级前固定各侧 USB 物理端口，版本相同默认跳过，勾选“同版本也重新烧录主摄”可覆盖；升级后回读版本，并核对物理端口、硬件 SN 和控制端点包长未变化；USB iSerial 属于固件标识，允许随镜像变化，目标固件版本另行严格回读匹配。
- 主摄升级前或刷写后设备版本读取失败时，Web 弹窗等待确认（最多 5 分钟）；确认并输入维护密码后可跳过该次读取继续，取消或超时终止后续操作，已刷写固件不回滚。等待期间保留维护锁且数采保持停止。镜像版本读取失败、已读版本不匹配以及端口/SN/MaxPacket 检查失败仍终止；跳过刷写后版本验证时结果明确标记“设备版本未验证”，日志保留失败原因与选择。同版本覆盖选项不自动授权跳过读取失败。
- 主摄升级独立读取镜像版本与所选设备版本，镜像检查不依赖摄像头在线；仅明确 CLI 参数/usage 错误才兼容旧命令，设备缺失或镜像读取失败不重试，保留原命令、退出码及输出。Web 缺摄预检和设备版本错误显示具体 /dev/cam_left 或 /dev/cam_right，避免误报镜像失败。
- 主摄 DFU 强制调用 `/usr/local/lib/ugripper-web/bin/dfu-util`，不回退系统 PATH；夹爪是串口 Ymodem OTA，不使用 DFU。
- 夹爪：单份 HEX 顺序升级左右夹爪，校验 HEX checksum/地址跨度，固定 `/dev/left_gripper`、`/dev/right_gripper`，禁止自动选串口；两次版本回读一致才成功。
- metadata 3.3 在 data_version 后包含 13 个版本字符串（新增 rk3588_firmware_version 位于 ego_firmware_version 后、hardware_list 前，来自 /home/user/version.info，复用现有版本缓存；历史 3.3 缺少该新增字段仍兼容）；ego_bound（布尔值）紧邻 ego_software_version 之前，hardware_list 末项为 ego_sn（未绑定为空）。主摄固件版本取 USB iSerial；胸摄未启用、+nostereo 双目固件写 "disabled"；未绑定 Ego 两个版本为空。起录前所有启用设备版本必须已知，缺项拒绝创建 episode，待机继续限频重试；起录冻结版本和绑定 SN，停录只消费内存副本。其余 metadata 字段、顺序、类型及 calibration 格式保持不变。Ego 拉回并完成远端大小校验与清理后，在本地 ego/metadata.json 写 related_episode_name 和 related_episode_uuid，分别等于 UMI episode_name（不带 -temp）和 data_uuid。 范本：`.codex/skills/validate-episode-data/references/metadata-v3.3.json`。
- 固件信息：UGripper 自动发布 `/dev/shm/ugripper/firmware_status.json`，Web 仅读取左右主摄 USB iSerial 和夹爪版本，不提供手动读取操作。夹爪版本复用运行时 HMI 驱动的串口，在启动、重连或升级后服务恢复时读取；失败每 30 秒限频重试，录制中只显示缓存。设备断开清除该侧展示，状态快照超过 15 秒显示待更新；主摄只读取 sysfs，未知/失败不会回退到旧维护任务缓存。
- 编码器读数：配置页开始后通过 `POST /api/v1/encoder/stream` 持续接收 NDJSON，串口保持连接，目标 100 Hz，显示实际频率；浏览器绘制受屏幕帧率限制。停止/隐藏/离开页面会取消请求，网络写入超时 1 秒，每次约 60 秒兜底释放并恢复串口参数。仅读 `0x0041`，CRC 校验，不写零位。原始计数保留无符号寄存器值，角度和弧度按与录制驱动一致的有符号 16 位计数换算（大于等于 32768 时减 65536）；例如 61364 显示约 −22.92° / −0.4000 rad，不将负角度转换成 0–360°，也不裁剪到机械行程范围。读取期间与录制、维护互斥。
- 编码器标零：仅配置页提供左/右独立按钮，请求必须携带 `side=left/right`，缺少或指定 both 均拒绝；弹窗明确所选侧并要求确认指定治具。只检测并执行所选侧，不会在无治具的自动测试中写零位。
- 设备参数：四个下拉框对应 `/etc/environment` 的 `UGRIPPER_LANG`（zh/en）、`CAMERA_CODEC`（h265/h264）、`ENABLE_CHEST_CAM_MAIN`（false/true）、`UGRIPPER_POWER_LINE_FREQUENCY`（50/60）。保存前弹窗显示参数和数采重启告警，录制/下载/维护时拒绝修改。现有 root worker 调用 root 专属 `environment_config.py` helper，等价承担 sudo 提权边界；Web 服务不直接写文件，也不授予任意 sudo 命令。helper 固定路径、严格枚举值校验、原子替换并保留权限和其他配置；数采恢复后生效。
- 批量硬件升级不是原子事务：若第二侧失败，界面保留第一侧结果，整批报告失败，不自动回滚已成功的一侧。

## 依赖与验证

`production/` 的相机适配器、C helper 源码和串口 OTA 来源于 production_tools（2026-09-14 引入）；构建不依赖该外部工作区。DFU 使用 Ubuntu Jammy arm64 dfu-util 0.9-1，依赖系统 libc/libusb，已有主包提供 libusb 依赖；许可证见 `bin/dfu-util.COPYING`。

局部检查：`/usr/bin/python3 -m unittest discover -s standalone/UgripperWebServer/tests -v`；页面脚本：`node --check standalone/UgripperWebServer/static/app.js`。

板端验证需检查 HTTPS 免登录访问、跨站写请求拒绝、路径穿越、上传上限、双向录制互斥、完整下载/断点续传、固件升级版本与身份回读、任务恢复、文件清理及服务重启。数据下载为单文件或流式目录 tar，正在写入的 `*-temp` 目录不开放。

### 手动相机抓图

设备概览提供“获取本机相机图片”和独立的“获取 Ego 图片”按钮。抓图与录制、维护共用互斥锁，不停止主服务或双目预热。

- 本机：读取 `/opt/ugripper/bin/CameraRecorder/config/camera_recorder.yaml`（兼容 flat / schema v1），使用采集宽高、帧率、输入格式及输出缩放/过滤配置；主摄编码、胸部开关和电源频率读取 `/etc/environment`。主摄 H.265 通过 GStreamer 采集短码流后解码，其余相机直接用 FFmpeg 抓一帧；双目通过现有 Fays 控制 FIFO 的 `GETIMG|<32位hex请求ID>` 获取下一帧，SDK 仍由原取流线程独占，控制线程负责保存。每侧双目输出一张双眼拼接图，与录制图像布局一致。
- Ego：选择绑定设备或唯一在线 Ego；已有 `episode_*-temp` 时拒绝短录。记录本次新建目录，短录约 3 秒，STOP 后等待 finalize，拉取 RGB / ctrl / tracking 视频并核对大小，从视频 1 秒处截帧。三张图片全部成功后仅删除本次远端测试 episode；失败时保留数据及 `ego_test.json` 证据，清理失败显式报告。
- 命令行：`getimg [输出目录]`；仅 Ego 使用 `getimg --ego [输出目录]`。默认输出 `/tmp/getimg_年月日_时分秒`，结果清单为 `result.json`，部分失败返回非零。
- Web 后台异步执行，返回图片预览和逐路失败原因。Web 图片保存在 `/var/lib/ugripper-web/snapshots/<请求ID>/`；下一次抓图会清理上次本地结果。Ego 远端清理失败不会被本地清理自动重试。
- 本机图片为触发后各自获取的帧，不承诺跨相机同时曝光。双目清单包含帧时间戳；其他本机相机仅提供请求与完成时间，Ego 提供截帧视频位置。

### Ego 版本与盖板损坏检测

- Ego 绑定读取 `ro.internal.display.id` 固件版本与 `com.ssnwt.helloxr` 的 `versionName` 软件版本；已绑定设备的在线探测每分钟更新一次版本缓存，旧绑定自动补读，断连页面标记缓存。
- 触觉区域按左/右夹爪及左/右触觉传感器显示，保留具体 camera key 与 USB serial。`盖板损坏检测` 按采集参数抓四路无损 PNG 图片，以运行时同样的 FFmpeg crop/scale/gray 预处理，分别对比 reference 与 persistent，输出基线/当前图/mask 状态图。正常、疑似损坏、基线不匹配、无基线和采集错误分别显示；即时结果不写入连续 episode 黄灯窗口。
- 基线重置仅使用最近两分钟已展示且采集成功的图片，提交时重新核对 USB serial，用户须在弹框中确认盖板正常且无接触，无需维护密码。主运行时等待后台校验完成后处理固定请求文件，更新所选 reference / persistent / both 基线、对应历史和黄灯；不停止服务或双目预热。另一基线的告警保留。
- 检测调用正式包 `bin/tactile_damage_detector/detect`，沿用系统 Python 的 NumPy/Pillow；不安装或修改运行环境依赖。例图以单张实拍图片展示，不包含基线或 mask 拼图；损坏例图采用指定的 `20260404_0021_DAP91126320032BF_left_tcam_l.jpg`，来源见 `static/tactile-examples.json`。
- 只保留最新一次 Web 图片、预处理帧、mask 和结果；下一任务清理旧结果。Ego 本地短视频在成功、失败或超时后清理。维护服务启动清理旧预览与未完成基线请求；基线重置的请求、回复和中间帧完成后清理。正式长短期基线与固定例图持久保留。

每张触觉图片标题旁显示实时告警标签（黄：盖板损坏告警；绿：无损坏告警；灰：未知/未连接），随状态接口刷新。已保存图片的 SN 与当前设备不一致时提示重新取图。告警来源为连续检测历史，图片下方另列本次检测结果；连接中断时标签转为未知。


### 生产压测与语言

右上角中文/English 临时切换，刷新后跟随设备 `UGRIPPER_LANG`。日志原文保留。

配置页“启动生产压测”顺序执行 A (120+20 秒) ×51 和 B (300+20 秒) ×11，名义总计 10660 秒，另加启动和收尾。失败阈值：A ≥3、B ≥2、总计 >3，任一成立即不通过，但继续跑完。每轮检查正式质量结论、时长（目标 -2 至 +10 秒）、必需文件和验证错误日志。每轮最多等待启动 30 秒、停止收尾 90 秒。无法安全停止则中断整个任务，不继续发 START。

设备后台执行，关闭浏览器或拔网线不影响；再次访问查看最新结果、轮次、失败原因和剩余时间。“取消压测”停止本次录制并保留结果，取消和服务中断均不判通过。报告保存在 `/var/lib/ugripper-web/production.json`，`production-results/` 留存最近 20 次；正式 episode 保留在数据盘，请保证容量并勿同时操作实体录制按钮。

API：`POST /api/v1/production/start`（confirm=true）、`POST /api/v1/production/cancel`（id），状态包含在 `GET /api/v1/status` 的 production 字段。压测期间 Web 维护、抓图、下载、编码器会话与 USB 升级互斥。

Web 右上角语言选择器固定显示 `Language / 语言` 标签；录制数据列表在分页前按名称倒序排列，最新日期/编号的 episode 优先展示。

生产压测区域直接显示总进度条、已完成轮数（成功或失败均计入）、当前组内轮次、阶段和阶段剩余时间，每 3 秒随设备状态刷新。A 组 120 秒录制 + 20 秒停止 × 51 轮，B 组 300 秒录制 + 20 秒停止 × 11 轮。取消中的未完成轮次不计入已完成进度；断连显示进度未更新，重新连接后读取设备端持久化任务状态。启动和收尾可能增加实际耗时。

## 时间同步测试

Web 启动任务时读取 `/etc/environment` 的 `DEVICE_SN`，转小写后选择数据盘下 `<sn>/data`，显式传入脚本的 `--episode-root`。未配置 SN 时与运行时一致使用 `noname_device/data`；目录不存在或 SN 非法直接拒绝，不扫描其他设备目录。状态刷新保留每组日志节点、展开及滚动状态，新任务清空旧详情。

配置页填写每组录制时长（1–3600 秒）、测试组数（1–100）、毫秒表延迟（非负整数秒，小于录制时长），默认 100 秒、1 组、2 秒。连接 GPIO139 外接毫秒表后点击开始。沿用已保存的编码/电源频率配置，每组执行高电平清零 5 秒、START、延迟拉低毫秒表、到时 STOP、高电平清零 5 秒、等待 episode 收尾。录制时长和延迟均从 START 指令计时；硬件起录耗时可能使视频略短。

页面显示组数、阶段剩余时间、每组目录与日志。关闭网页继续运行；停止会等待录制和毫秒表收尾，任一组失败即结束后续组。不要同时操作实体录制按钮。复用生产压测独占锁和 USB 升级锁；不会把正常录制所需的维护锁持有整个任务。维护服务重启将复位毫秒表并停止已知的本任务录制，标记中断，不自动续跑。

结果保存在 `/var/lib/ugripper-web/time-sync.json`，`time-sync-results/` 保留最近 20 次报告和日志。数据保留在正常数据目录，每组增加 `stopwatch_timing.json`（实际 GPIO 写入边界、START/STOP 时间）。采集完成只代表录制与文件检查成功，需另行标定毫秒表评估同步误差。

API：`POST /api/v1/time-sync/start`，JSON 为 `{"duration_s":100,"count":1,"delay_s":2}`；`POST /api/v1/time-sync/cancel`，JSON 为 `{"id":"任务 ID"}`。写请求仍需 `X-UGripper-Request: 1` 且通过 Origin 检查。`GET /api/v1/status` 的 `time_sync` 字段提供结果。

## 升级电量门禁

WebUI 与外部 API 的软件、主摄、夹爪及 RK3588 启动固件升级统一要求背包电量至少 **30%**：29% 拒绝，30% 允许；充电及强制升级均不豁免。任务接收前、停服前、实际安装及每侧烧录前读取蓝牙服务已有 `/opt/bluetooth_server/bms_latest.txt`，电量缺失、非法或源时间戳超过 20 秒时拒绝。网页同步禁用全部升级按钮并提示；外部应用可通过录制状态接口的 `battery_ready`、`can_upgrade` 预检查，详见 [设备 API](../../docs/device-api.md)。

已经开始的单次烧录不因电量下降中断，下一侧烧录前重新检查。独立重启指令和重启后只读校验不受此门限影响。

## RK3588 启动固件升级

升级页“3588 设备固件升级”支持两种上传方式：直接选择 `.tar.gz` / `.tgz` / `.tar` 发布包；或在 PC 解压后选择包含 `update.sh`、`SHA256SUMS` 和 `payload/` 的整个发布目录。目录由浏览器保留路径与内容打包上传，无须手动重新压缩。不再接受单独 `boot.img`。上传及解压总量各须小于 200 MiB，最多 256 个归档条目；只接受普通文件/目录，拒绝链接、越界路径及重复文件。

- root worker 按 SHA256SUMS 校验包内文件；发布标识取带板型前缀的 version.info，或保留的发布目录名。RB1021 对应 V100，RB1021V130 对应 V130，精确匹配。镜像、驱动及配置文件名称由发布包决定，Web 不另设固定清单或解析镜像。
- 持有录制/维护/USB 升级互斥锁并停数采后，以 `/bin/sh -e update.sh` 执行厂商脚本（命令失败即中止）；同版本也执行。脚本负责更新整个发布包，Web 不再自行执行旧的裸镜像写入流程。电量至少 30%，不可用/过期拒绝。
- 示例 `RB1021_CFTC_RK3588_V1.0.3.2_20261022143700_alpha.tar.gz` 的实际版本取自 `payload/version.info`：`V1.0.3.2_20261022143700_release`，不根据包名推断。其脚本写 boot、调整文件系统、替换 Wi-Fi 驱动与启动脚本、覆盖 rsyslog/logrotate 配置并重启 rsyslog；V100 还可能替换 fstab、调整/挂载数据分区，并清空 `/home/ubuntu/Downloads/`。这些是包内脚本行为，升级会影响原有日志配置。
- 脚本返回成功后核对 /home/user/version.info 与包内目标版本，匹配才进入 awaiting_reboot；失败不自动重启。包内脚本不应自行重启。
- 执行前保存目标版本、清单哈希、任务 ID 和 boot ID。重启后确认新 boot ID 和安装版本；服务重启保持等待，不重刷。boot_verified 表示重启及版本确认完成，不表示分区逐字节回读。
- Runtime 按现有版本缓存周期读取 `/home/user/version.info`，字段为 `rk3588_firmware_version`，Web 概览、统一/指定版本 API 共用 `firmware_status.json`。文件缺失/内容无效显示未识别，不填默认版本；起录前版本必须已知，起录冻结后随 metadata 写入，`data_version` 保持 `3.3`，原有字段顺序和格式不变。
- 没有 A/B 回滚或 Web 救砖功能；升级不能断电，无法启动时需走设备既有离线恢复流程。回读成功不代替外设功能验收。

### 录制触觉告警图像留存

概览页“当前触觉告警图像”仅在当前传感器处于黄灯告警且 SN 与留存证据一致时显示正式停录检测时的 mask，标明左右夹爪/传感器、SN、短期或长期基线、episode 与时间；底图为同一次判定实际使用的 160×120 预处理盖板灰度帧，红色半透明 mask 标出参与异常判定的核心像素或结构异常块；正常区域保留底图，异常区域也保留底图纹理。它与手动“盖板损坏检测”预览分开，不会重新抓取实时图像来替代当时证据。

同一 episode 中处于连续三次告警且本次检测仍异常的传感器组成一组，保存在 `/var/lib/ugripper/tactile_state/latest_alarm.json`；下一组原子覆盖，始终只保留最新一组（最多四路、两类基线各一张），没有新告警不清除上一组。正常、断连、身份变化时隐藏对应图像；后台保留最新证据。旧软件产生的告警不能补出 mask，升级后下一次符合条件的停录检测会生成。留存独立于 episode metadata，手动检测/基线重置不删除历史证据，服务重启后，仍处于匹配告警状态时可查看。升级/同版本重装沿用现有 prerm 清理 tactile_state 的行为，会同时清除基线、历史及该留存。
