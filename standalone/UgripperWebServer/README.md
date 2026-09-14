# UGripper Web 设备维护

独立 HTTPS 页面服务与本地维护 worker，源代码位于此 standalone 目录。

- 页面：`https://<设备以太网地址>:8080/`。默认绑定 `192.168.2.240`，不监听 Wi-Fi 或通配地址。
- 接上网线即可直接进入页面，无账号、密码或登录步骤。首次安装只生成证书和 `/etc/ugripper-web/config.json`；覆盖安装保留网口配置和证书，清理旧密码哈希及 `initial-password` 文件。
- HTTPS 使用设备自签证书，首次访问可能需要浏览器接受证书；生产交付可替换为受客户电脑信任的证书。指纹可用 `openssl x509 -in /etc/ugripper-web/server.crt -noout -fingerprint -sha256` 核对。
- HTTPS 服务以 ubuntu 运行；独立 root worker 使用 `/usr/local/lib/ugripper-web/` 的 root 专属代码与工具副本及系统 Python（包内提供 pyserial），仅监听本地 Unix socket，不提供 shell/任意命令接口。
- 所有连接设备以太网的用户均可查看状态、下载数据/日志并操作维护功能。写请求由页面自动携带公开固定头 `X-UGripper-Request: 1`，校验 Origin 且不开放 CORS，以阻止跨站页面触发操作；不使用 Cookie、密码或会话。升级确认、治具确认与互斥锁始终生效。
- 状态仅读取设备节点、系统统计和 Fays 状态快照；“已识别”不等于固件通信健康。双目帧的新鲜度使用最近帧时间，缺失证据不会显示为正常出流。

## 互斥与任务

`/run/ugripper/maintenance.lock` 为永不删除的 flock inode。录制从创建 episode 前持有至收尾结束；数据下载从打开文件至响应结束持有；维护从接受任务至设备恢复和文件清理结束持有。旧 `/tmp/umi_recording.lock` 状态语义保留。USB updater 同时使用该锁和自己的实例锁。

录制中拒绝上传/升级/标零/下载；下载中拒绝开始录制。状态查看不锁定设备。断线、进程退出释放文件锁；刷写任务在客户端断线后继续完成。重启后的未完成任务标记失败，不自动重试烧录。自更新由独立 worker 执行，维护锁持续覆盖 dpkg 与 postinst，任务完成后才重启 worker。

上传体积严格小于 200 MiB，流式落盘；上传文件使用随机编号，worker 复制到 root 私有目录后校验。结束后清除固件/deb，保留任务摘要和日志。每台设备只保留一份待提交上传；服务重启清除未提交文件。

- 软件白名单与 `../DASUsbUpdater` 一致：das-usb-updater、ugripper、bluetooth-gatt-server、databot-device-joint、device-ota-mender。按 deb 内部 Package 字段校验，接受 arm64/all。通过 `dpkg --no-act --install` 预检，再使用 `dpkg --install` 覆盖安装，不在线下载依赖。
- 主摄：单份 IMG 顺序升级左右主摄；升级前固定各侧 USB 物理端口，版本相同默认跳过，勾选“同版本也重新烧录主摄”可覆盖；升级后回读版本，并核对 USB serial、SN 和控制端点包长未变化。
- 主摄 DFU 强制调用 `/usr/local/lib/ugripper-web/bin/dfu-util`，不回退系统 PATH；夹爪是串口 Ymodem OTA，不使用 DFU。
- 夹爪：单份 HEX 顺序升级左右夹爪，校验 HEX checksum/地址跨度，固定 `/dev/left_gripper`、`/dev/right_gripper`，禁止自动选串口；两次版本回读一致才成功。
- 固件信息：主摄左右分别显示实时 USB iSerial；夹爪左右分别显示最近读取版本及时间，未读取/查询失败明确显示未知。点击“读取夹爪版本”确认后，在维护锁内停止数采进行只读串口查询，再恢复服务。每侧升级成功立即保存该侧版本，不因另一侧失败丢失结果。
- 编码器标零：仅配置页提供左/右独立按钮，请求必须携带 `side=left/right`，缺少或指定 both 均拒绝；弹窗明确所选侧并要求确认指定治具。只检测并执行所选侧，不会在无治具的自动测试中写零位。
- 设备参数：四个下拉框对应 `/etc/environment` 的 `UGRIPPER_LANG`（zh/en）、`CAMERA_CODEC`（h265/h264）、`ENABLE_CHEST_CAM_MAIN`（false/true）、`UGRIPPER_POWER_LINE_FREQUENCY`（50/60）。保存前弹窗显示参数和数采重启告警，录制/下载/维护时拒绝修改。现有 root worker 调用 root 专属 `environment_config.py` helper，等价承担 sudo 提权边界；Web 服务不直接写文件，也不授予任意 sudo 命令。helper 固定路径、严格枚举值校验、原子替换并保留权限和其他配置；数采恢复后生效。
- 批量硬件升级不是原子事务：若第二侧失败，界面保留第一侧结果，整批报告失败，不自动回滚已成功的一侧。

## 依赖与验证

`production/` 的相机适配器、C helper 源码和串口 OTA 来源于 production_tools（2026-09-14 引入）；构建不依赖该外部工作区。DFU 使用 Ubuntu Jammy arm64 dfu-util 0.9-1，依赖系统 libc/libusb，已有主包提供 libusb 依赖；许可证见 `bin/dfu-util.COPYING`。

局部检查：`/usr/bin/python3 -m unittest discover -s standalone/UgripperWebServer/tests -v`；页面脚本：`node --check standalone/UgripperWebServer/static/app.js`。

板端验证需检查 HTTPS 免登录访问、跨站写请求拒绝、路径穿越、上传上限、双向录制互斥、完整下载/断点续传、固件升级版本与身份回读、任务恢复、文件清理及服务重启。数据下载为单文件或流式目录 tar，正在写入的 `*-temp` 目录不开放。
