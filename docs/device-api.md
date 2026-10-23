# 设备 API 汇总

地址沿用 WebUI：`https://<设备IP>:8080`。GET 无需登录；POST 携带 `X-UGripper-Request: 1`、`X-UGripper-Maintenance-Password: <维护密码>` 和 JSON 请求体。外部升级无需弹窗或 `confirm` 字段。浏览器 Origin 仍需同源；原生客户端可不发送 Origin。自签名证书示例用 `curl -k`，正式接入可信任设备证书。

| 方法与路径 | 用途 |
| --- | --- |
| `GET /api/v1/versions` | 全部软件、硬件及固件版本 |
| `GET /api/v1/versions?fields=ugripper,hardware_version,cam_left_firmware_version` | 指定版本，英文逗号分隔 |
| `GET /api/v1/devices` | 全部硬件连接与识别信息 |
| `GET /api/v1/devices?ids=left_gripper,cam_left` | 指定设备 |
| `GET /api/v1/recording` | 当前录制阶段及维护可用性 |
| `POST /api/v1/upgrades` | 使用已放好的包直接强制升级，返回任务 ID |
| `GET /api/v1/upgrades/{id}` | 查询该任务的进度与最终结果 |
| `POST /api/v1/upgrades/{id}/reboot` | 单独指令重启，JSON 为 `{}` |

## 版本查询

`fields` 支持任意已安装 deb 软件包名（可带 `:arm64` 等架构限定），例如 `das-usb-updater`、`ugripper`、`bluetooth-gatt-server`、`databot-device-joint`、`device-ota-mender`。不传 `fields` 返回全部已安装包和下列 metadata 版本字段；以后现有版本缓存新增的字段也自动可查询。

| metadata 字段 | 来源/含义 |
| --- | --- |
| `hardware_version` | Runtime 使用的硬件版本，例如 `v2.5.0` |
| `software_version` | ugripper 版本，保持 metadata 的 `v` 前缀 |
| `das_usb_updater_version` | das-usb-updater，兼容旧名 ugripper-usb-updater |
| `data_version` | metadata 格式版本，目前 `3.3` |
| `bluetooth_gatt_server_version` | bluetooth-gatt-server |
| `databot_device_joint_version` | databot-device-joint |
| `device_ota_mender_version` | device-ota-mender |
| `gripper_left_firmware_version` / `gripper_right_firmware_version` | 左右夹爪固件 |
| `cam_left_firmware_version` / `cam_right_firmware_version` / `cam_chest_firmware_version` | 主摄 USB **iSerial** |
| `stereo_left_firmware_version` / `stereo_right_firmware_version` | 左右双目固件 |
| `ego_software_version` / `ego_firmware_version` | 绑定 Ego 的 App/系统固件 |
| `rk3588_firmware_version` | `/home/user/version.info`，由 Runtime 缓存的背包设备固件版本 |

```json
{"versions":{"ugripper":{"version":"2.1.15","status":"ready","source":"dpkg","updated_at_ms":1792300000000},"hardware_version":{"version":"v2.5.0","status":"ready","source":"/dev/shm/ugripper/firmware_status.json","updated_at_ms":1792300001000}}}
```

`status`：`ready` 已知、`disabled` 未启用（version 为 `disabled`）、`unrecognized` 缓存有效但版本未识别、`stale` 缓存过期、`unavailable` 缓存/数据库不可用、`not_installed` 未安装的软件包。未知值为 `null`，过期值不冒充当前版本。版本值从设备读取，示例值不作为默认值。

软件读取 dpkg 已有状态数据库，并按文件变化刷新进程内缓存；固件/硬件读取 Runtime 现有 `firmware_status.json`，有效期 15 秒。不会主动查询串口、相机或 ADB，不另建版本快照。`rk3588_firmware_version` 同时写入 metadata 3.3（位于 `ego_firmware_version` 后、`hardware_list` 前），其余字段格式及数据版本不变。指定查询：`GET /api/v1/versions?fields=rk3588_firmware_version`，缺失/非法版本按未识别返回，不使用包名或默认值。

## 硬件状态

固定包含以下 16 项，设备未接入时也返回条目：

- `left_gripper`、`right_gripper`
- `cam_left`、`cam_right`、`cam_chest`
- `left_fays_stereo`、`right_fays_stereo`
- `tcam_left_l`、`tcam_left_r`、`tcam_right_l`、`tcam_right_r`
- `left_fays_imu`、`right_fays_imu`
- `left_encoder`、`right_encoder`
- `ego`

```json
{"devices":{"cam_left":{"id":"cam_left","path":"/dev/cam_left","serial":"SN123","enabled":true,"connected":true,"identified":true,"stream":null,"status":"connected","connection_source":"device_node","updated_at_ms":1792300001000,"source":"/dev/shm/ugripper/firmware_status.json","error":""}}}
```

`connected`、`identified`、`enabled` 为 `true/false/null`；`null` 表示未知，不能当作已连接。普通设备 `status` 包括 `connected/disconnected/unrecognized/disabled/unknown/stale/unavailable`。连接正常但必需 SN 未读出为 `unrecognized`。

`connection_source=device_node` 表示设备节点存在，不代表已验证出流；夹爪使用 `hmi_activity` 活跃状态。双目/IMU 的 `stream` 复用现有 Fays 出流缓存，IMU 的 SN 来源于所属双目，标记 `identity_source=stereo_camera`。编码器不提供独立 SN，`identified=null`，不伪造身份；其余无出流缓存的设备 `stream=null`。Ego 复用绑定状态，附加 `bound`，状态为 `connected/disconnected/unbound/unknown`。

## 录制状态

```json
{"state":"idle","active":false,"episode_dir":null,"updated_at_ms":1792300001000,"maintenance_available":true,"battery_ready":true,"can_upgrade":true}
```

- `state`：`idle` 空闲、`starting` 起录、`recording` 录制、`finalizing` 收尾、`active` 旧缓存仅能确认会话占用、`unknown` 状态不可确认。
- `active=true` 包括起录和收尾；未知为 `null`。`episode_dir` 来自现有录制锁。
- `battery_ready` 表示有效电量缓存达到 30%；`can_upgrade` 仅在已确认空闲且录制/维护/生产测试互斥均可用、无等待重启任务且电量缓存有效并达到 30% 时为 true；仅供预检查。真正执行时再次获取锁，冲突返回 HTTP 409，强制升级也不打断录制。软件升级完成到维护服务重启期间，新提交同样返回 409，待服务恢复后重试。

## 放包与强制升级

临时目录固定为 `/var/lib/ugripper-web/upgrade-packages/`，root/ubuntu 组可写。外部应用先写 `.part` 等临时名称，写完后在同目录原子 rename 成最终文件名，再提交。只接受目录内单个普通文件名，不接受绝对路径、子目录、符号链接或命令。包必须非空且小于 200 MiB。其他用户可由管理员授予该目录写权限。

```json
{"kind":"software","package":"ugripper_2.1.15_arm64.deb","sha256":"<可选的64位小写SHA256>"}
```

| kind | 文件 | 执行范围 |
| --- | --- | --- |
| `software` | `.deb` | 复用 Web 安装白名单：上述五个软件包；支持同版本覆盖、降级安装 |
| `main_camera` | `.img` | 左右主摄依次强制烧录，同版本也烧录 |
| `gripper` | `.hex` | 左右夹爪依次升级并回读版本 |
| `rk3588_boot` | 包含 `update.sh`、`SHA256SUMS`、`payload/` 的 `.tar.gz` / `.tgz` / `.tar` 发布包 | 校验清单及硬件后执行包内脚本，同版本也执行；核对版本、文件和 boot 分区后等待重启 |

`rk3588_boot` 保留原 kind 名称，但不再接受裸 `boot.img`。外部程序将完整归档放入临时目录后提交，例如 `{"kind":"rk3588_boot","package":"RB1021_CFTC_RK3588_V1.0.3.2_20261022143700_alpha.tar.gz"}`。PC 解压后的目录可直接通过 WebUI 上传；原生 API 使用归档文件。包内版本取 `payload/version.info`，安装后位于 `/home/user/version.info`；任务 `result.firmware_version` 返回目标版本。脚本更新范围包含 boot、驱动、启动脚本及系统配置，具体以发布包为准；样例还会调整文件系统、覆盖日志配置及清空 `/home/ubuntu/Downloads/`。包内脚本不得自行重启，脚本、版本、文件和分区校验均成功才进入 `awaiting_reboot`，独立重启前清理临时包，新启动后再次核对。详见 [Web 升级说明](../standalone/UgripperWebServer/README.md)。

POST 不接受任意 shell 命令。无需 `confirm`，`force` 固定为 true。主摄设备版本读取失败自动继续，保留 `version_checks_skipped` 及回读未验证证据；镜像合法性、设备匹配、USB/SN 回归检查仍执行。所有软件/固件升级要求背包电量可用且至少 **30%**（30% 允许），低于 30%、快照缺失或过期均拒绝，充电和强制升级不豁免；任务接收前、停服前及实际安装/逐侧烧录前均检查。软件查询范围不限于升级白名单。

接受返回 HTTP 202：

```json
{"ok":true,"id":"0123456789abcdef0123456789abcdef","status":"running","status_url":"/api/v1/upgrades/0123456789abcdef0123456789abcdef"}
```

**202 仅表示接受任务，必须轮询任务结果。**

| 任务 status | 含义 |
| --- | --- |
| `running` | 执行中，`success=null` |
| `success` | 执行、校验和服务恢复完成，`success=true` |
| `failed` | 失败，`success=false`；查看 `error_code/message/results` |
| `awaiting_reboot` | 固件写入及回读成功，`success=true/reboot_pending=true/boot_verified=false`；等待重启指令 |
| `rebooting` | 重启指令已接受；启动后的校验尚未完成 |

任务包含 `id/kind/stage/started/updated/finished`（时间为 Unix 秒），及 `result`、逐侧 `results`、`sha256` 等适用字段。终态在文件清理和服务恢复后发布；`success` 不保证被明确跳过的主摄版本回读已验证。左右升级无法原子回滚，失败时应核对逐侧结果。

3588 写入成功不会自动重启。调用 reboot 路由后约 3 秒重启，返回 202 `accepted=true/status=rebooting`；此时禁起录。重连后继续查询原任务，只有新启动后分区校验通过才有 `boot_verified=true/status=success`。该标志确认新启动后的 boot 分区完整性，不表示所有外设已验收。调度失败返回错误；60 秒仍未重启回到 `awaiting_reboot` 并记录 `reboot_timeout`，可重发重启指令。维护服务重启不等同整机重启，不提前报告启动后校验成功。

已接受的源文件移入 root 私有区，任务结束清理。**重启指令执行前和新开机恢复时清空整个临时升级目录**，包括未提交包；同一次开机仅重启维护服务不会清空该目录。任务结果保存在 `/var/lib/ugripper-web/upgrade-results/{id}.json`，boot 恢复标记在 `/var/lib/ugripper-maintenance/boot-pending.json`，均不在临时目录内。

HTTP 错误：400 参数/文件/前置校验失败，403 请求校验/密码错误，404 任务不存在，409 录制/维护/测试/U盘升级冲突，500 服务内部错误。任务错误码包括 `upgrade_failed/service_restore_failed/cleanup_failed/maintenance_interrupted/maintenance_restart_failed/boot_verification_failed/reboot_schedule_failed/reboot_timeout`；主摄详细原因保留在逐侧结果中。无法连接或请求超时不能推断升级成功，应恢复连接后查询任务。

## 调用示例

```sh
base='https://192.168.2.240:8080'
curl -k "$base/api/v1/versions?fields=hardware_version,das-usb-updater,ugripper,bluetooth-gatt-server,databot-device-joint,device-ota-mender"
curl -k "$base/api/v1/devices?ids=left_gripper,cam_left,left_fays_imu,left_encoder"
curl -k "$base/api/v1/recording"
# 确认 can_upgrade=true 后；包须已放好。维护密码放入环境变量 MAINTENANCE_PASSWORD。
curl -k -X POST "$base/api/v1/upgrades" \
  -H 'X-UGripper-Request: 1' \
  -H "X-UGripper-Maintenance-Password: $MAINTENANCE_PASSWORD" \
  -H 'Content-Type: application/json' \
  -d '{"kind":"software","package":"ugripper_2.1.15_arm64.deb"}'
# 替换为实际返回的 id，轮询至明确结果。
curl -k "$base/api/v1/upgrades/<id>"
# 仅当 awaiting_reboot；此请求就是明确的重启指令。
curl -k -X POST "$base/api/v1/upgrades/<id>/reboot" \
  -H 'X-UGripper-Request: 1' \
  -H "X-UGripper-Maintenance-Password: $MAINTENANCE_PASSWORD" \
  -H 'Content-Type: application/json' -d '{}'
```
