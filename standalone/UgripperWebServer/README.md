# UGripper Web 设备维护

独立 HTTPS 页面服务与本地维护 worker，源代码位于此 standalone 目录。

- 页面：`https://<设备以太网地址>:8080/`。默认绑定 `192.168.2.240`，不监听 Wi-Fi 或通配地址。
- 接上网线即可直接进入页面，无账号、密码或登录步骤。首次安装只生成证书和 `/etc/ugripper-web/config.json`；覆盖安装保留网口配置和证书，清理旧密码哈希及 `initial-password` 文件。
- HTTPS 使用设备自签证书，首次访问可能需要浏览器接受证书；生产交付可替换为受客户电脑信任的证书。指纹可用 `openssl x509 -in /etc/ugripper-web/server.crt -noout -fingerprint -sha256` 核对。
- HTTPS 服务以 ubuntu 运行；独立 root worker 使用 `/usr/local/lib/ugripper-web/` 的 root 专属代码与工具副本及系统 Python（包内提供 pyserial），仅监听本地 Unix socket，不提供 shell/任意命令接口。
- 所有连接设备以太网的用户均可查看状态、下载数据/日志并操作维护功能。写请求由页面自动携带公开固定头 `X-UGripper-Request: 1`，校验 Origin 且不开放 CORS，以阻止跨站页面触发操作；不使用 Cookie、密码或会话。升级确认、治具确认与互斥锁始终生效。
- 状态仅读取设备节点、系统统计和 Fays 状态快照；“已识别”不等于固件通信健康。双目帧的新鲜度使用最近帧时间，缺失证据不会显示为正常出流。

- Ego 卡片与 HWS 共用 `ego_status.py`，显示绑定、连接和绑定序列号；仅读取 `/dev/shm/ugripper/ego_binding*.json`。连接快照须匹配绑定序列号且不超过 15 秒，过期/异常显示未知；未绑定不检测连接，USB 存储模式不代表 ADB 已连接。

## 互斥与任务

`/run/ugripper/maintenance.lock` 为永不删除的 flock inode。录制从创建 episode 前持有至收尾结束；数据下载从打开文件至响应结束持有；维护从接受任务至设备恢复和文件清理结束持有。旧 `/tmp/umi_recording.lock` 状态语义保留。USB updater 同时使用该锁和自己的实例锁。

录制中拒绝上传/升级/标零/下载；下载中拒绝开始录制。状态查看不锁定设备。断线、进程退出释放文件锁；刷写任务在客户端断线后继续完成。重启后的未完成任务标记失败，不自动重试烧录。自更新由独立 worker 执行，维护锁持续覆盖 dpkg 与 postinst，任务完成后才重启 worker。

上传体积严格小于 200 MiB，流式落盘；上传文件使用随机编号，worker 复制到 root 私有目录后校验。结束后清除固件/deb，保留任务摘要和日志。每台设备只保留一份待提交上传；服务重启清除未提交文件。

- 软件白名单与 `../DASUsbUpdater` 一致：das-usb-updater、ugripper、bluetooth-gatt-server、databot-device-joint、device-ota-mender。按 deb 内部 Package 字段校验，接受 arm64/all。通过 `dpkg --no-act --install` 预检，再使用 `dpkg --install` 覆盖安装，不在线下载依赖。
- 主摄：单份 IMG 顺序升级左右主摄；升级前固定各侧 USB 物理端口，版本相同默认跳过，勾选“同版本也重新烧录主摄”可覆盖；升级后回读版本，并核对物理端口、硬件 SN 和控制端点包长未变化；USB iSerial 属于固件标识，允许随镜像变化，目标固件版本另行严格回读匹配。
- 主摄 DFU 强制调用 `/usr/local/lib/ugripper-web/bin/dfu-util`，不回退系统 PATH；夹爪是串口 Ymodem OTA，不使用 DFU。
- 夹爪：单份 HEX 顺序升级左右夹爪，校验 HEX checksum/地址跨度，固定 `/dev/left_gripper`、`/dev/right_gripper`，禁止自动选串口；两次版本回读一致才成功。
- 固件信息：UGripper 自动发布 `/dev/shm/ugripper/firmware_status.json`，Web 仅读取左右主摄 USB iSerial 和夹爪版本，不提供手动读取操作。夹爪版本复用运行时 HMI 驱动的串口，在启动、重连或升级后服务恢复时读取；失败每 30 秒重试、每次连接最多 3 次，录制中只显示缓存。设备断开清除该侧展示，状态快照超过 15 秒显示待更新；主摄只读取 sysfs，未知/失败不会回退到旧维护任务缓存。
- 编码器读数：配置页开始后通过 `POST /api/v1/encoder/stream` 持续接收 NDJSON，串口保持连接，目标 100 Hz，显示实际频率；浏览器绘制受屏幕帧率限制。停止/隐藏/离开页面会取消请求，网络写入超时 1 秒，每次约 60 秒兜底释放并恢复串口参数。仅读 `0x0041`，CRC 校验，不写零位。读取期间与录制、维护互斥。
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
- 基线重置仅使用最近两分钟已展示且采集成功的图片，提交时重新核对 USB serial，用户须确认盖板正常且无接触。主运行时等待后台校验完成后处理固定请求文件，更新所选 reference / persistent / both 基线、对应历史和黄灯；不停止服务或双目预热。另一基线的告警保留。
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
