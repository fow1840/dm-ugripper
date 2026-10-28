# 设备 API 汇总

更新于 **2026-10-27**，按当前 2.1.15 开发工作区的 HTTP 路由与 worker 实现核对；不保证 2.1.11–2.1.14 安装包已提供全部接口。本文涵盖外部专用查询/升级接口，以及可由原生客户端调用的 Web 共用接口。

地址沿用 WebUI：`https://<设备IP>:8080`。GET 无需登录；所有 POST 必须携带 `X-UGripper-Request: 1`。仅上传、execute、主摄版本确认、外部升级及其 reboot 路由额外要求 `X-UGripper-Maintenance-Password: <维护密码>`。除原始二进制上传外，POST 使用 JSON 对象请求体，即使没有参数也发送 `{}`。外部升级无需弹窗或 `confirm` 字段。浏览器 Origin 仍需同源；原生客户端可不发送 Origin。自签名证书示例用 `curl -k`，正式接入可信任设备证书。

| 方法与路径 | 用途 |
| --- | --- |
| `GET /api/v1/versions` | 产品相关软件、硬件及固件版本（默认精简） |
| `GET /api/v1/versions?fields=ugripper,hardware_version,cam_left_firmware_version` | 指定版本，英文逗号分隔 |
| `GET /api/v1/devices` | 全部硬件连接与识别信息 |
| `GET /api/v1/devices?ids=left_gripper,cam_left` | 指定设备 |
| `GET /api/v1/recording` | 当前录制阶段及维护可用性 |
| `POST /api/v1/upgrades` | 使用已放好的包直接强制升级，返回任务 ID |
| `GET /api/v1/upgrades/{id}` | 查询该任务的进度与最终结果 |
| `POST /api/v1/upgrades/{id}/reboot` | 单独指令重启，JSON 为 `{}` |

## 请求与兼容约定

- 默认监听设备以太网地址的 8080 端口，实际地址由设备 Web 配置决定。无需 Cookie、登录会话或 Bearer token；维护密码逐次校验。
- 所有 POST 要求有效 `Content-Length`，不接受 `Transfer-Encoding: chunked`。普通 JSON 请求体必须非空且严格小于 4096 字节；`/upload` 使用原始文件体，严格小于 200 MiB。
- GET 返回 JSON，下载/图像除外；编码器流返回逐行 JSON。HTTP 错误统一为 `{"error":{"code":"稳定错误码","message":"可读说明","retryable":false}}`；成功响应仍按各接口合同，不假定都含 `ok`。
- `fields`、`ids` 支持逗号分隔或重复 query 参数，去重后最多 128 项；每项 1–128 字符，仅允许字母、数字及 `_.+:-`，首字符必须为字母或数字。空项/非法字符返回 400；未知设备 ID 返回 400 `unknown_device`，未知包名按 `not_installed` 返回。
- `/recording` 只查询状态，**普通录制没有通用 START/STOP HTTP 路由；轨迹点检使用专用接口**。后文生产/授时测试会主动录制，但不是普通录制控制接口。
- `/status` 默认返回精简摘要，`recording` 为与 `/recording` 相同的对象。`/status?details=true` 显式返回完整 Web 状态；其中 `versions/devices` 与专用查询使用相同对象结构，UI 用设备标签列表另放 `device_cards`。摘要与详情的 `recording` 均与 `/recording` 使用相同对象结构。

## 版本查询

`fields` 支持任意已安装 deb 软件包名（可带 `:arm64` 等架构限定），例如 `das-usb-updater`、`ugripper`、`bluetooth-gatt-server`、`databot-device-joint`、`device-ota-mender`。不传 `fields` 仅返回五个产品软件包名、`hardware_version`、`data_version` 和下列固件字段，共 17 项；不重复返回软件包对应的 metadata 别名，不自动加入系统包。显式 `fields` 仍支持 metadata 别名与任意包名。`scope=all` 才额外枚举全部已安装包；`details=true` 才返回每项的 `source/updated_at_ms`。非法 scope/details 返回 400。

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
{"versions":{"ugripper":{"version":"2.1.15","status":"ready"},"hardware_version":{"version":"v2.5.0","status":"ready"}}}
```

`status`：`ready` 已知、`disabled` 未启用（version 为 `disabled`）、`unrecognized` 缓存有效但版本未识别、`stale` 缓存过期、`unavailable` 缓存/数据库不可用、`not_installed` 未安装的软件包。未知值为 `null`，过期值不冒充当前版本。版本值从设备读取，示例值不作为默认值。

默认每项固定为 `version/status`，软件统一用包名呈现；`software_version` 等 metadata 别名仅在显式 fields 查询时返回。

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
{"devices":{"cam_left":{"serial":"SN123","enabled":true,"connected":true,"identified":true,"stream":null,"status":"connected"}}}
```

默认设备以固定 ID 为对象键，不重复输出 id；字段固定为 `serial/enabled/connected/identified/stream/status`，Ego 另有 `bound`。未知或空 SN 统一为 `null`。`details=true` 增加路径、来源、时间及错误诊断字段。

`connected`、`identified`、`enabled` 为 `true/false/null`；`null` 表示未知，不能当作已连接。普通设备 `status` 包括 `connected/disconnected/unrecognized/disabled/unknown/stale/unavailable`。连接正常但必需 SN 未读出为 `unrecognized`。

`connection_source=device_node` 表示设备节点存在，不代表已验证出流；夹爪使用 `hmi_activity` 活跃状态。双目/IMU 的 `stream` 使用 UVC/FT601 共用的只读状态解析：设备节点存在、recorder PID 存活、`running=true`，且状态写入时间和对应图像/IMU 最近样本时间均在 3 秒内才为 true。状态缺失或过期为 false；不能将 `connected=true` 等同于正在出流。IMU 的 SN 来源于所属双目，标记 `identity_source=stereo_camera`。编码器不提供独立 SN，`identified=null`，不伪造身份；其余无出流缓存的设备 `stream=null`。Ego 复用绑定状态，附加 `bound`，状态为 `connected/disconnected/unbound/unknown`。

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

单文件下载另可能返回 206（范围响应）或 416（范围无效）。HTTP 错误：400 参数/文件/前置校验失败，403 请求校验/密码错误，404 任务不存在，409 录制/维护/测试/U盘升级冲突，500 服务内部错误。任务错误码包括 `upgrade_failed/service_restore_failed/cleanup_failed/maintenance_interrupted/maintenance_restart_failed/boot_verification_failed/reboot_schedule_failed/reboot_timeout`；主摄详细原因保留在逐侧结果中。无法连接或请求超时不能推断升级成功，应恢复连接后查询任务。

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


## Web 共用接口清单

以下接口同样可以通过 HTTPS 调用，但返回结构随 Web 功能演进；接入时应按实际安装版本核对。表中“密码”指额外维护密码头，所有 POST 仍需要固定请求头和同源检查。

| 方法与路径 | 参数/请求体 | 密码 | 返回与用途 |
| --- | --- | --- | --- |
| `GET /api/v1/status` | 无 | 否 | 默认摘要；`details=true` 获取完整 Web 状态及测试结果 |
| `GET /api/v1/data` | `path`、`offset` | 否 | 数据目录列表 |
| `GET /api/v1/download` | `path` | 否 | 单文件或目录 tar |
| `GET /api/v1/logs` | 无 | 否 | 日志 tar.gz |
| `POST /api/v1/snapshot` | `{"kind":"local"}`；也支持 `ego`、`tactile` | 否 | 202，返回抓图 `id` |
| `GET /api/v1/snapshot` | `id`、`file`（默认 `result.json`） | 否 | 抓图结果或图片 |
| `GET /api/v1/tactile/alarm-mask` | `id`、`camera`、`scope` | 否 | 告警盖板底图叠加 mask 的 PNG |
| `POST /api/v1/tactile/reset` | `id/camera/scope/serial/confirm` | 否 | 用本次检查结果重置指定触觉基线 |
| `POST /api/v1/encoder/read` | `{}` | 否 | 左右编码器单次读数 |
| `POST /api/v1/encoder/stream` | `{}` | 否 | 200，`application/x-ndjson` 持续读数 |
| `POST /api/v1/time-sync/start` | `duration_s/count/delay_s` | 否 | 202，启动毫秒表录制测试 |
| `POST /api/v1/time-sync/cancel` | `{"id":"任务ID"}` | 否 | 取消对应测试 |
| `POST /api/v1/production/start` | `{"confirm":true}` | 否 | 202，启动固定生产录制压测 |
| `POST /api/v1/production/cancel` | `{"id":"任务ID"}` | 否 | 取消对应压测 |
| `POST /api/v1/upload` | 原始二进制文件体，非 multipart | 是 | 201，返回 `upload/size/sha256` |
| `POST /api/v1/execute` | `kind/confirm` 及对应参数 | 是 | 202，启动 Web 维护任务 |
| `POST /api/v1/camera/version-confirm` | `id/token/accepted` | 是 | 回应当前主摄版本读取失败确认 |
| `POST /api/v1/camera/version-cancel` | `id/token` | 否 | 拒绝该确认，终止后续升级步骤 |

### 聚合状态与新鲜度

`/status` 默认仅包含 `recording`、`battery`、`task`、`production`、`time_sync`。后三项只返回存在的任务 ID、状态、阶段、时间、轮次及剩余时间等标量字段，不含日志、轮次列表、图片或完整结果。

`/status?details=true` 顶层包含 `device_sn`、`versions`、`device_versions`、`firmware`、`devices`、`device_cards`、`cpu_percent`、`memory`、`disks`、`busy`、`recording`、`service_active`、`battery`、`configuration`、`ego`、`tactile`、`tactile_alarm`、`task`、`production`、`time_sync`。CPU 首次采样可能为 null；内存与磁盘容量为字节。`busy=false` 只是当前检查结果，不保证下一次维护请求能获取锁。

摘要适合持续轮询，详细状态按需获取；Web 页面显式请求 details=true 来展示完整诊断与测试记录。`task` 为当前 Web 维护任务；生产和时间同步测试分别读取 `production`、`time_sync`。外部升级使用 `/upgrades/{id}` 查询持久化任务，不能仅凭聚合状态的最新任务判断历史任务成败。建议按需轮询，Web 通常约 3 秒刷新；不要把轮询失败当作设备断连的最终证据。

### 数据及日志下载

`path` 相对于设备数据根目录（默认 `/mnt/data_disk`），例如 `<设备SN小写>/data/episode_20261027_0001`，不是相对于某一 episode。空 path 表示根目录。不接受绝对路径、空路径段、`.`、`..`、符号链接及任何 `-temp` 路径段；数据盘必须已挂载。

`/data` 返回 `{"entries":[{"name":"...","size":123,"directory":false}],"offset":0,"has_more":false}`。名称倒序，每批检查 200 个目录项；`offset` 默认 0、负值按 0，下一页增加 200。过滤临时目录后返回数量可能少于 200，是否继续以 `has_more` 为准；目录的 size 不是递归总大小。

`/download` 的普通文件为 `application/octet-stream`，支持单段 `Range: bytes=起点-终点` 或 `bytes=起点-`；不支持后缀范围或多段范围。目录为流式 `application/x-tar`，跳过临时目录，不支持 tar 断点续传。`/logs` 为压缩日志归档。目录浏览、下载和日志获取都会获取维护互斥锁；下载整个响应期间阻止录制，录制中请求会返回 409。

```sh
curl -k --get "$base/api/v1/data" --data-urlencode 'path=<设备SN小写>/data' --data-urlencode 'offset=0'
curl -k --get "$base/api/v1/download" \
  --data-urlencode 'path=<设备SN小写>/data/episode_20261027_0001' -o episode.tar
curl -k "$base/api/v1/logs" -o ugripper-logs.tar.gz
```

### 抓图、触觉告警与基线

抓图提交后轮询 `/snapshot?id=<id>&file=result.json`，按结果中的图片文件名请求同一路由。提交响应附 `status_url`，可直接轮询该地址获取结果；原 `/snapshot?id=...` 仍用于图片及结果读取。结果或图片已清理返回 HTTP 404 `snapshot_not_found`。任务 ID 为 32 位小写十六进制；图片文件名只允许字母、数字、下划线、连字符以及 `.jpg/.png` 后缀。下一次抓图会删除前一次结果，应及时下载。抓图与录制/维护互斥，本机图片不保证同时曝光；Ego 抓图会实际短录并拉取图片，失败时可能保留远端诊断数据。

`kind=tactile` 为手动盖板检查，结果不写入连续三次损坏历史。重置请求必须来自该次抓图结果，传入匹配的 `id`、`camera`、USB `serial`，`scope` 为 `reference` 或 `persistent`，且 `confirm=true` 表示确认盖板正常且无接触。该操作会修改基线，不能当作只读检测。

实时告警读取 `/status?details=true` 的 `tactile_alarm`，使用其组 `id` 及 `images` 中的 `camera/scope` 请求 `/tactile/alarm-mask`。告警组可能被新结果替换，旧 ID 请求失败时重新取状态，不混用两组图片。该图像接口与手动抓图结果目录不同。

### 编码器与自动测试

编码器单次读数返回 `left/right` 对象，每侧包含 `status`；成功包含 `position`（无符号原始寄存器）、`angle_deg`、`angle_rad`、`read_at`（Unix 秒），不可用时附 `message`。角度按有符号 16 位计数换算；Web 原始计数也按有符号显示（例如寄存器 61364 显示 -4172）。API `position` 保留寄存器原值，不能直接当成弧度。

编码器流逐行返回左右读数，以及 `rate_hz`、`dropped`、`remaining_s`。目标约 100 Hz，每次约 60 秒释放；断开连接可结束读取。每行使用 `type=sample/error/end`：sample 保留左右读数等原字段；error 包含 `error.code/message/retryable`；正常到达会话时限发送 `{"type":"end","reason":"duration_limit"}`。异常 error 是终态，不再发送正常 end；连接中断而未收到终态表示未确认结束。串口读取期间持有互斥锁，不写零位。

时间同步测试请求示例为 `{"duration_s":100,"count":1,"delay_s":2}`：时长 1–3600 秒、组数 1–100、延迟为非负整数且小于时长；需要 GPIO139 毫秒表。完整结果查询 `/status?details=true` 的 `time_sync`。生产测试使用固定 A/B 录制计划，完整结果查询 `/status?details=true` 的 `production`，具体计划见 [Web 维护说明](../standalone/UgripperWebServer/README.md)。两者均会真实录制和占用设备，浏览器/客户端退出不自动取消；需要发送对应 cancel 请求并等待收尾。

### Web 上传与维护流程

外部自动升级优先使用前文 `/upgrades`。如无法直接放包，也可先向 `/upload` 发送原始文件，取得 `upload` token，再调用 `/execute`。每台设备仅保留一个未提交上传，新上传清理旧文件；上传 token 不是外部 `/upgrades` 的文件名，不能混用。

`/execute` 必须 `confirm=true`，支持：

| kind | 额外字段 | 行为 |
| --- | --- | --- |
| `software/main_camera/gripper/rk3588_boot` | `upload`；可选布尔 `force` | Web 升级流程；与外部接口固定强制策略不同 |
| `zeroing` | `side="left"` 或 `"right"`、`fixture="FIXTURE_READY"` | 仅所选侧编码器标零，必须实际安装指定治具 |
| `configuration` | `settings` | 保存四项设备参数并恢复数采服务 |

`settings` 必须恰好包含以下四个字符串字段，不接受部分更新或额外字段：

```json
{"UGRIPPER_LANG":"zh","CAMERA_CODEC":"h265","ENABLE_CHEST_CAM_MAIN":"false","UGRIPPER_POWER_LINE_FREQUENCY":"50"}
```

允许值分别为 `zh/en`、`h265/h264`、`false/true`、`50/60`。注意布尔开关在这里是字符串。维护任务详细结果读取 `/status?details=true` 的 `task`；主摄版本读取失败的确认须使用当前任务给出的 id/token，`accepted=true` 才授权跳过该次读取，最多等待 5 分钟。外部 `/upgrades` 自动处理该确认，不需要再调用确认路由。

## 接入依据

本文核对入口为 [server.py](../standalone/UgripperWebServer/server.py)、[device_api.py](../standalone/UgripperWebServer/device_api.py)、[maintenance.py](../standalone/UgripperWebServer/maintenance.py)、[fays_status.py](../standalone/UgripperWebServer/fays_status.py) 及对应状态/抓图/测试模块。接口合同以源码为准；轨迹点检录制与通知已在 240 实测，未执行升级、基线重置或标零测试。


## 统一错误与任务查询约定

当前未对外发布的 API 直接使用新错误格式；Web 已同步，不提供旧字符串错误的兼容层。HTTP 错误码如下：

| code | HTTP | 含义 |
| --- | --- | --- |
| `invalid_request` | 400 | 请求格式或业务前置校验失败，具体看 message |
| `invalid_selector/unknown_device/invalid_details/invalid_scope` | 400 | 查询参数不合法 |
| `forbidden` | 403 | 固定请求头、Origin 或维护密码校验失败 |
| `not_found` | 404 | 接口不存在 |
| `task_not_found/snapshot_not_found` | 404 | 任务或抓图结果不存在、已被替换或清理 |
| `device_busy` | 409 | 录制、维护或测试互斥冲突，retryable=true |
| `invalid_range` | 416 | 单文件下载范围无效 |
| `internal_error` | 500 | 内部异常，先检查日志及原任务结果 |

`retryable=true` 表示冲突解除后可重试，不表示服务端已自动重试。升级请求超时或返回内部错误时，不能盲目再次提交；先核查原任务，避免重复写入。任务结果内部的 `error`、`reason` 等诊断字段与 HTTP 错误对象不同，仍按任务自身格式解析。

抓图、生产测试、授时测试和 Web execute 提交成功时统一附加 `status_url`，客户端直接使用返回 URL，无需拼装：

```json
{"ok":true,"id":"0123456789abcdef0123456789abcdef","status_url":"/api/v1/tasks/production/0123456789abcdef0123456789abcdef"}
```

`GET /api/v1/tasks/{kind}/{id}` 的 kind 为 `production`、`time-sync`、`maintenance`、`snapshot`，GET 无需密码。查询结果沿用对应任务的原始结果结构，不强行改写其业务状态：例如生产测试有 `passed/cancelled/interrupted`，抓图有 `ok/partial/failed`，不能仅判断是否 `success`。

生产/授时查询只匹配当前保留任务的 ID；旧任务被替换后返回 404，不返回新任务冒充旧结果。维护查询优先读取按 ID 保存的结果，再核对当前任务；抓图读取对应结果目录，其生命周期仍为下一次抓图前。外部升级继续使用提交响应已有的 `/upgrades/{id}` 查询地址。这里只统一查询入口，不新增任务历史库或自动重试。

时间字段按单位解析：查询缓存的 `updated_at_ms` 是 Unix 毫秒；维护任务 `started/updated/finished` 和编码器 `read_at` 是 Unix 秒；抓图 `*_at_ns` 是 Unix 纳秒；`remaining_s` 为剩余秒数，不是绝对时间。未为此重写已有任务存储字段。

最小错误处理示例：

```sh
curl -k -sS 'https://192.168.2.240:8080/api/v1/versions?fields=ugripper' | jq
# 需要自动判断时同时检查 HTTP 状态；message 供人阅读，程序分支使用 error.code。
# 有副作用的请求不要配置无条件自动重试。
```


## 点检（编码器开合与轨迹测试）

Web“点检”页提供编码器开合检查和轨迹测试录制，与 App 使用相同接口。

| 接口 | JSON | 行为 |
| --- | --- | --- |
| `POST /api/v1/inspection/encoder/start` | `{}`；固定双手检查 | 202，后台检查人工 5 次完整开合 |
| `POST /api/v1/inspection/encoder/cancel` | `{"id":"任务ID"}` | 取消本任务，未完成不判通过 |
| `POST /api/v1/inspection/trajectory/start` | `{}` | 202，立即请求轨迹测试起录，API 无倒计时 |
| `POST /api/v1/inspection/trajectory/stop` | `{"id":"任务ID"}` | 请求停止对应测试会话；后台收尾，响应不代表已完成 |
| `GET /api/v1/tasks/inspection/{id}` | 无 | 查询当前点检任务；非当前 ID 返回 404 |

POST 使用 `X-UGripper-Request: 1`，无维护密码，仍检查 Origin。提交返回 id/status_url；/status 提供 inspection 摘要，details=true 提供完整检查记录。编码器执行状态 status 与检查结论 result 分开，completed 不等于 pass。关闭页面不会取消已提交任务；worker 重启不自动续跑，任务标记中断。

### 编码器测试操作与 API 交互（2026-10-28）

1. 接入左右夹爪，保持数采服务运行，设备处于可执行点检的空闲状态。在 Web 点检页点击“开始检查”，或调用下面的 start 接口。
2. 收到任务 ID 后开始查询任务；`202` 表示任务已接受，后台初始化和采样是否成功以查询结果为准。`status=running` 时持续展示两侧实时计数、阶段及轮次；异步失败会返回 `status=failed`、`result=error` 和 `code/message`。
3. 双手先充分张开，再轻合；随后各完成五次轻合、充分张开。每次充分到位后再反向运动，系统在反向动作确认后记录上一半周期极值。第五次张开后再开始合拢，直到页面确认第五轮。
4. 两侧独立计数，每侧完成五轮后蓝灯闪两次并短鸣两次，表示该侧采样结束。等两侧结束后读取最终 `result` 和全部 `failures`。
5. 中途退出检查时调用 cancel，再查询到终态。关闭网页本身不会取消检查。服务重启导致的中断需重新开始。

终端示例（需要 `jq`；查询和取消使用 start 返回的同一任务 ID）：

```sh
base='https://192.168.2.240:8080'
reply=$(curl -ksS --fail-with-body -X POST "$base/api/v1/inspection/encoder/start" \
  -H 'X-UGripper-Request: 1' -H 'Content-Type: application/json' -d '{}')
printf '%s\n' "$reply"
# HTTP 202 示例：{"id":"任务ID","status_url":"/api/v1/tasks/inspection/任务ID"}
id=$(printf '%s' "$reply" | jq -er '.id')

# 周期查询：建议每 200ms 一次，进入终态后停止轮询。
curl -ksS "$base/api/v1/tasks/inspection/$id" | jq .

# 用户需要取消时调用：
curl -ksS --fail-with-body -X POST "$base/api/v1/inspection/encoder/cancel" \
  -H 'X-UGripper-Request: 1' -H 'Content-Type: application/json' \
  -d "$(jq -nc --arg id "$id" '{id:$id}')"
# 接受取消示例：{"id":"任务ID","stopping":true}
# 接着查询同一 status_url，直到 status 不再为 running。
```

start 非 2xx 时按统一 `error.code/message/retryable` 处理，不继续使用空 ID 查询。查询失败或超时可重试；404 表示该 ID 已不可查询，需要检查当前任务。编码器结果通过轮询获取。

| 查询字段 | 调用方处理 |
| --- | --- |
| `status=running` | 检查进行中，继续查询 |
| `status=completed` | 检查结束，结合 `result` 判断；超时收集不足也可能是 completed/incomplete |
| `status=cancelled/interrupted/failed` | 已取消、已中断或执行失败，展示原因和已采集结果 |
| `result=pass/fail/incomplete/error` | 分别为通过、不通过、采集不完整、执行错误 |
| `sides.left/right` | 各侧阶段、实时计数、逐轮峰值与一致性结果 |
| `failures` | 展示全部不通过项 |

每轮 `value` 是有符号原始计数；`consistency[].peaks_deg` 是五轮同类端点相对编码器零位的角度，公式为 `计数 × 360 / 65536`。`max_difference_deg` 才是五轮峰值的最大差异，例如峰值计数 `[56,48,48,48,56]` 对应差异 `0.043945°`。

### 编码器判定规则

编码器固定检查双手，左右独立累计五次闭合、张开。请求 `{}`，也接受 `{"side":"both"}`；left/right 返回 HTTP 400，message 为 inspection_requires_both_sides。使用有符号 16 位原始计数，闭合半周期取最大值、张开半周期取最小值；先进入端点识别区（闭合峰值 ≥ -1000，张开峰值 ≤ -3000），再从当前极值反向移动至少 500 计数并持续 0.1 秒后，确认反向动作，结算上一半周期的峰值。行程中的停顿只保留当前极值，继续同方向移动时继续更新。

双手先充分张开，再开始轻合，准备阶段的张开作为起点；随后完成五次轻合、充分张开，最后再开始合拢，以确认第 5 次张开的峰值。两侧都获得五组闭合/张开结果后结束，检查最多 180 秒。

闭合 [-100,150]，小于/大于分别 closed_below_range/closed_above_range；张开 [-4200,-3650]，小于 -4200 为 open_above_range（幅度过量），大于 -3650 为 open_below_range（幅度不足）。边界通过。每项包含 cycle/endpoint/value/limits/status/code；不足五次为 cycles_incomplete，读取异常 encoder_read_failed。

编码器五轮峰值一致性按左右手、闭合/张开分别检查，共四组；每组比较五轮同类峰值的任意两次差异：5 次闭合最大值之间比较、5 次张开最小值之间比较，按 `计数 × 360 / 65536` 换算成角度。各侧夹爪 SN 第五位产品版本为 `1`（V2.5）时限值 2°，为 `3`（V2.9）时限值 1°，等于限值通过。范围判定与一致性判定同时执行，任一不通过时最终 result=fail；任务仍收集五轮完整结果。

任务开始读取并冻结左右夹爪 SN、版本和一致性限值；顶层 `grippers.left/right` 分别返回 `serial/hardware_version/consistency_limit_deg`，`consistency_limits_deg.left/right` 返回各侧限值。各侧新增 `consistency` 两项，字段为 `endpoint/count/peaks_deg/max_difference_deg/limit_deg/status/code`；peaks_deg 按轮次列出同类峰值（角度），max_difference_deg 为该组任意两次峰值的最大差异。不足五个峰值时为 pending，结束仍不足为 incomplete。一致性通过/失败分别为 pass/fail，失败码为 `closed_consistency_exceeded` 或 `open_consistency_exceeded`。

顶层 `failures` 汇总全部不通过项，逐轮范围失败带 side/cycle/endpoint/code，一致性失败带 side/endpoint/code；Web 逐项显示，不用单个 code 代替完整失败列表。例如同侧可同时返回：

```json
{"failures":[{"side":"left","cycle":2,"endpoint":"closed","code":"closed_above_range"},{"side":"left","endpoint":"closed","code":"closed_consistency_exceeded"},{"side":"left","endpoint":"open","code":"open_consistency_exceeded"}]}
```

夹爪未连接、SN 缺失/格式不合法或状态超过 15 秒时报告 `gripper_sn_unavailable`，SN 产品版本未配置规则时报告 `gripper_sn_unsupported`，均带 side；对应一致性状态为 unavailable。任务中夹爪 SN/规则变化时以 interrupted/gripper_identity_changed 结束，重新开始检查使用新夹爪身份。取消、超时或读取错误也保留已得到的范围和一致性结果。

Web 轨迹按钮先倒数 5 秒，取消或离开点检页即取消尚未提交的倒计时；倒数结束才发送 API。API 立即请求起录但仍受正常设备就绪门禁约束，不保证零启动耗时。手动停止只作用于同一任务 ID 的会话。最终通过仅指原有录制 QC 通过，不代表自动验证轨迹形状。

metadata 3.3 在 episode_name 后立即增加字符串 test_dataset_name：空字符串为标准数据，trajectory_test 为轨迹测试，production_test 为生产压测。起录冻结标识，失败数据保留；普通下一条恢复为空。只增加这一字段，其他 metadata 字段和 data_version 不变；历史 3.3 缺该字段按标准数据解释。测试任务 ID 仅保存在运行状态，不添加到 metadata。
### Web 点检结果面板

左右手分别展示固定5轮闭合/张开峰值表、完成进度和两项一致性结果；通过显示绿色PASS，失败显示红色FAIL，待检测/未完成显示灰色。全部失败项集中列出。启动响应返回id后即可取消；取消接受后显示“正在取消”，任务终态确认结束，请求失败会显示原因并允许重试。

编码器点检在设备/维护服务重启恢复时以 interrupted/service_interrupted 结束；主录制服务停止或重启也会通过运行实例标识变化终止点检并释放串口及互斥锁。Web显示“已中断 · 请重新开始”，恢复开始按钮，已记录结果保留。

### 夹爪 SN 版本与混用门禁

夹爪 SN 为16位大写字母/数字、DAG前缀，**第五位（索引4）**为产品版本：`1` 对应 V2.5，`3` 对应 V2.9。点检阈值使用各侧夹爪SN，不读取 UMI 整机 hardware_version 来推断夹爪。`/versions` 中的整机版本及 metadata 原有硬件版本字段保持原含义。

左右夹爪 V2.5/V2.9 混用时，旧版侧 `/devices` 的 status 为 `gripper_version_mismatch`，details=true 附 error；`/status?details=true` 的 firmware.gripper 对应侧包含 hardware_version、status=error、error=gripper_version_mismatch。Web 设备概览标红旧版侧。点检 failures 同样返回旧版侧混用项，并保留逐轮及一致性结果。

混用作为 runtime 健康故障禁止所有起录入口；若录制期间检测到混用，沿原有硬件故障收尾链路结束录制。错误码 UGR-40-07，错误灯指向旧版侧；版本冲突不触发USB自动复位。更换为同版本组合后重新检测并恢复。

每侧编码器完成5轮后由maintenance向runtime发送绑定任务ID、侧别和5秒有效期的内部完成通知；runtime核对该侧done状态并去重，执行两次蓝灯闪烁及4000Hz短鸣（每次至少350ms、间隔至少250ms，分阶段等待夹爪蜂鸣器状态回报，每阶段最多等待1500ms），随后恢复当前灯态。提示表示该侧检查结束，结论查看页面；录制、硬件故障和Ego同步提示优先。Web点检实时计数独立轮询任务约5次/秒，与后台0.2秒快照配合，失败重试，请求3秒超时，复用既有采样。

编码器点检仅覆盖写入固定的 `/var/lib/ugripper-web/inspection.json`，供实时展示和读取最近结果；每轮不创建任务历史文件、采样文件或独立日志。正常完成、取消与中断都使用同一路径，新任务覆盖旧结果，旧ID查询404。维护服务启动先恢复中断结果，再清理旧版遗留的编码器历史JSON及原子写入临时文件；


### 编码器完成提示的静音保障

完成提示复用现有增强关闭链路 `silenceBeepForSide → confirmBeepSilenced`：正常完成或因录制、健康故障、Ego 同步等优先状态结束提示时，发送 OFF 并请求新状态，只有新回报 `duty=0` 才确认静音，最多重试 5 次。固件静音时可以保留非零 frequency，确认以 duty 为准。Runtime 正常析构也对仍在提示的侧执行静音。

底层蜂鸣状态切换按 5 次发送计划重发；关闭后每秒补发 OFF，连接建立和驱动正常退出也发送关闭状态。阶段回报超时后仍进入结束静音链路。静音确认失败会记录告警；任务的 completed/pass 表示采样和判定结果，不是蜂鸣器静音确认回执。

此前 240 板端回放已观察到左右两侧 on/off/on/off 四阶段状态回报，最后 duty=0。当前代码覆盖正常控制链路；进程被 SIGKILL、设备掉电或串口断联时无法保证立即执行关闭，恢复连接后由初始化关闭机制处理。此类故障不能仅凭正常流程实测宣称绝不会持续蜂鸣。
