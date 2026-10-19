'use strict';
// Preserve source text per node so temporary language switches never alter settings.
const translations={
'确认重启设备':'Confirm device reboot',
'固件写入校验成功，等待重启指令':'Firmware written and verified; awaiting reboot command',
'固件已写入并校验。确认重启设备？临时升级包将被清理，网页会暂时断开。':'Firmware has been written and verified. Reboot now? Temporary upgrade packages will be deleted and the page will disconnect.',
'重启指令已接受':'Reboot command accepted',
'重启调度失败，可重试重启指令':'Reboot scheduling failed; retry the reboot command',
'设备尚未重启，可重试重启指令':'Device has not rebooted; retry the reboot command',
'awaiting_reboot':'Awaiting reboot',

'背包电量低于 30%，禁止升级':'Backpack battery below 30%; update blocked','电量数据不可用或已过期，禁止升级':'Battery data unavailable or stale; update blocked','背包电量满足升级条件（至少 30%）':'Backpack battery meets the update requirement (at least 30%)',
'3588 启动固件升级':'RK3588 boot firmware update','上传并升级 3588 固件':'Upload and update RK3588 firmware',
'仅支持适配本背包硬件的 RK3588 FIT boot.img，不支持整盘镜像、rootfs 或主摄固件。写入校验成功后等待单独确认重启；请保持供电，勿拔插设备。相同镜像跳过写入和重启。':'Use an RK3588 FIT boot.img compatible with this backpack hardware. Full-disk, rootfs and camera images are not supported. After writing and verification, a separate reboot confirmation is required. Keep power and devices connected. Matching images skip writing and reboot.',
'，写入校验成功后等待单独确认重启':'; writing and verification complete before a separate reboot confirmation',
'检查 3588 启动镜像':'Checking RK3588 boot image','写入 3588 启动分区，请勿断电':'Writing RK3588 boot partition; keep power connected','回读校验 3588 启动分区':'Verifying RK3588 boot partition','等待设备重启，恢复连接后自动校验':'Waiting for reboot; verification resumes after reconnection','3588 启动固件重启后校验通过':'RK3588 boot firmware verified after reboot','3588 启动固件校验失败':'RK3588 boot firmware verification failed','启动分区已匹配，跳过写入与重启':'Boot partition already matches; skipped writing and reboot','重启后校验通过':'Verified after reboot',

'维护密码':'Maintenance password','请向设备管理员获取维护密码；每次操作均需输入。':'Ask the device administrator for the maintenance password. Enter it for each operation.','请输入维护密码':'Enter the maintenance password','维护密码错误，请重新输入':'Incorrect maintenance password; enter it again','请先确认已安装指定标零治具':'Confirm that the specified zero-calibration fixture is installed first',
'已与背包同步':'Synchronized with backpack','授时连接断开':'Time sync disconnected','授时异常':'Time sync error','正在与背包同步':'Synchronizing with backpack','与背包偏差':'Offset from backpack','往返延迟 RTT':'Round-trip time (RTT)','暂无测量':'No measurement','时间同步':'Time synchronization','持续同步中':'Continuously synchronized','旧版录制前校时':'Legacy pre-record sync','等待背包校时':'Waiting for backpack clock','等待同步':'Waiting for synchronization','偏差 / RTT':'Offset / RTT',
'当前触觉告警图像':'Current tactile alarm images','当前告警暂无留存图像，等待下一次录制检测':'No retained images for the current alarm; waiting for the next recording check','暂无留存告警图像':'No retained alarm images','结构异常区域':'Structural anomaly region','异常核心区域':'Anomaly core region',
'显示告警检测当时的盖板图像，红色半透明区域为异常 mask；仅保留最新一组告警。':'The cover image from the alarm check is shown with a translucent red anomaly mask. Only the latest alarm group is retained.',
'时间同步测试':'Time synchronization test','每组录制时长（秒）':'Recording duration per round (s)','测试组数':'Test rounds','毫秒表延迟启动（秒）':'Stopwatch start delay (s)',
'请连接外接毫秒表。延迟从发送录制开始指令计时，必须小于录制时长。每组前后自动清零 5 秒，沿用已保存的编码和电源频率配置。关闭网页后继续运行；完成后仍需标定确认同步误差。':'Connect the external stopwatch. The delay starts at the recording START command and must be shorter than the recording duration. Each round resets the stopwatch for 5 seconds before and after recording, using the saved codec and power-line settings. The test continues after closing the page. Calibrate the recordings to measure synchronization error.',
'开始时间同步测试':'Start synchronization test','停止时间同步测试':'Stop synchronization test','采集完成':'Recording complete','毫秒表清零中':'Resetting stopwatch','等待毫秒表启动':'Waiting to start stopwatch','毫秒表延迟必须小于录制时长':'Stopwatch delay must be shorter than recording duration','生产压测或时间同步测试正在执行':'Production or synchronization test running','日志':'Log',

'丢失 ':'Missed ','未收到有效读数':'No valid reading','设备管理':'Device management','正在连接':'Connecting','设备概览':'Overview','录制数据':'Recordings','升级维护':'Maintenance','配置':'Settings','空闲':'Idle','录制中':'Recording','维护 / 传输中':'Maintenance / transfer',
'左侧设备':'Left devices','右侧设备':'Right devices','相机抓图':'Camera snapshots',
'按采集参数获取本机相机图片，双目保持预热。Ego 单独短录约 3 秒，获取成功后删除设备上的本次测试数据。':'Capture using recording settings while keeping stereo warmup active. Ego records for about 3 seconds; its test recording is deleted after successful retrieval.',
'获取本机相机图片':'Capture local cameras','获取 Ego 图片':'Capture Ego','触觉盖板':'Tactile covers',
'检测前请张开夹爪，确保盖板无接触、无遮挡。红色 mask 标出疑似损坏区域；基线不匹配需人工核对。单次检测不会直接清除黄灯。':'Open the grippers and leave covers untouched and unobstructed. Red masks indicate suspected damage. Baseline mismatches require inspection. A single check does not clear the yellow warning.',
'盖板损坏检测':'Cover damage check','左夹爪':'Left gripper','右夹爪':'Right gripper','正常与损坏例图':'Normal and damaged examples',
'历史实拍样本，仅供外观对照；当前盖板请使用上方“盖板损坏检测”校验。':'Historical examples for visual reference. Use Cover damage check above to inspect the current covers.',
'正常盖板例图':'Normal cover example','损坏盖板例图':'Damaged cover example','正常触觉参考图':'Normal tactile reference','损坏盖板实拍例图':'Damaged cover photo',
'设备日志':'Device logs','获取日志包，供技术支持检查设备运行情况。':'Download logs for technical support.','获取日志':'Download logs','数据盘文件':'Data disk files','上一级':'Parent folder','上一页':'Previous','下一页':'Next',
'拉取数据期间不能录制；录制期间不能拉取数据。正在录制的目录不会展示。':'Recording and downloading are mutually exclusive. Active recording folders are hidden.',
'硬件版本':'Hardware version','未启用':'Disabled','左双目固件':'Left stereo firmware','右双目固件':'Right stereo firmware','胸部主摄固件':'Chest camera firmware',
'软件版本':'Software versions','固件信息':'Firmware information','左主摄固件（iSerial）':'Left main camera firmware (iSerial)','右主摄固件（iSerial）':'Right main camera firmware (iSerial)','主摄 iSerial 与夹爪固件版本由设备自动更新。':'Main camera iSerial and gripper firmware versions update automatically.',
'硬件、软件及相机/夹爪固件版本由设备自动更新，未启用设备单独标注。':'Hardware, software, camera and gripper firmware versions update automatically; disabled devices are labelled.',
'软件升级':'Software update','选择白名单内的 deb 软件包，支持同版本覆盖安装。':'Select an allowed deb package. Reinstalling the same version is supported.','上传并安装':'Upload and install','固件升级':'Firmware update','选择一份固件，自动逐侧升级全部对应设备。':'Select one firmware image to update both sides sequentially.',
'主摄固件 · 左右一起升级':'Main cameras · both sides','夹爪固件 · 左右一起升级':'Grippers · both sides','同版本也重新烧录主摄':'Reflash main cameras even if versions match','上传并升级':'Upload and update',
'文件须小于 200 MiB。升级期间请保持供电和设备连接。上传文件在任务完成后清理。':'Files must be smaller than 200 MiB. Keep power and devices connected during updates. Uploaded files are removed when the task ends.',
'设备参数':'Device settings','提示音语言':'Device language','视频编码':'Video codec','胸部主摄':'Chest camera','电源频率':'Power line frequency','zh · 中文':'zh · Chinese','false · 关闭':'false · Disabled','true · 开启':'true · Enabled',
'修改会影响录制行为。保存时将重启数采服务，请先停止录制并确认参数适合当前设备与环境。':'Changes affect recording. Saving restarts the recording service. Stop recording and check the settings before saving.','保存并应用':'Save and apply',
'编码器读数':'Encoder readings','开始读取':'Start reading','停止读取':'Stop reading','尚未读取':'Not read yet','左编码器':'Left encoder','右编码器':'Right encoder',
'100 Hz 持续读取当前位置，不修改零位。停止、离开页面或断连后释放串口；每次最多读取 60 秒。':'Read positions continuously at a target of 100 Hz without changing zero calibration. Serial ports are released on stop, page exit or disconnection. Each session lasts at most 60 seconds.',
'压测进度':'Test progress','已完成轮数：':'Completed rounds: ','当前组：':'Current group: ','当前轮：':'Current round: ','阶段剩余约 ':'Phase remaining: about ','进度更新时间：':'Progress updated: ','连接中断，进度未更新；重新连接后自动恢复':'Disconnected; progress is stale and will refresh after reconnection',
'生产压测':'Production stress test','A 组：120 秒录制 + 20 秒停止，共 51 轮（1 小时 59 分）。':'Group A: record 120 s + stop 20 s, 51 rounds (1 h 59 min).','B 组：300 秒录制 + 20 秒停止，共 11 轮（58 分 40 秒）。':'Group B: record 300 s + stop 20 s, 11 rounds (58 min 40 s).',
'不通过条件：A 失败 ≥3 次，或 B 失败 ≥2 次，或总失败 >3 次。达到阈值仍跑完全部轮次。启动与收尾会增加实际耗时。关闭网页或拔网线仍继续运行，重新连接可查看结果。':'Fail if A has ≥3 failures, B has ≥2, or the total exceeds 3. All rounds run even after a threshold is reached. Startup and finalization add time. The test continues after closing the page or unplugging Ethernet; reconnect to view results.',
'启动生产压测':'Start production test','取消压测':'Cancel test','尚未运行':'Not run yet','测试详情':'Test details',
'编码器标零':'Encoder zero calibration','重新设定编码器零位会改变所选侧现有标定。':'Resetting the encoder zero changes calibration for the selected side.','必须安装指定标零治具，并由了解标定流程的人员操作。':'Use the specified calibration fixture. Only trained operators should perform calibration.',
'左编码器标零':'Calibrate left encoder zero','右编码器标零':'Calibrate right encoder zero','任务进度':'Task progress','确认维护操作':'Confirm operation','我已安装指定标零治具，并确认设备姿态正确':'I have installed the specified fixture and verified the device position.','取消':'Cancel','确认执行':'Confirm',
'CPU 占用':'CPU usage','设备系统':'System','内存使用':'Memory usage','数据盘剩余':'Data disk free','系统盘剩余':'System disk free','未挂载':'Not mounted','总容量 ':'Capacity ','请连接数据盘':'Connect a data disk',
'电量不可用':'Battery unavailable','请检查蓝牙电量服务':'Check the Bluetooth battery service','电量':'Battery','背包电量':'Backpack battery','充电中':'Charging','放电中':'Discharging',
'未识别':'Not detected','已识别':'Detected','正常出流':'Streaming','暂未出流':'No stream','绑定状态':'Binding','未绑定':'Unbound','已绑定':'Bound','连接状态':'Connection','已连接':'Connected','未连接':'Disconnected','未检测（未绑定）':'Not checked (unbound)','未知（等待状态更新）':'Unknown (waiting for status)','未知':'Unknown','序列号':'Serial number','固件版本':'Firmware version','未读取':'Not read','（缓存）':' (cached)','未安装':'Not installed','版本不可用':'Version unavailable','等待自动读取':'Waiting for automatic read','状态待更新':'Waiting for status',
'数采服务在线':'Recording service online','数采服务已停止':'Recording service stopped','连接中断':'Connection lost','打开':'Open','下载':'Download',
'录制或维护进行中，请稍后下载':'Recording or maintenance in progress; download later.','录制或维护进行中，请稍后读取':'Recording or maintenance in progress; read later.','录制或维护进行中':'Recording or maintenance in progress',
'下载开始后请等待完成，期间无法录制。':'Wait for the download to finish before recording.','请选择小于 200 MiB 的文件':'Select a file smaller than 200 MiB','上传失败':'Upload failed','上传连接中断':'Upload connection lost','正在上传 ':'Uploading ',
'配置任务已提交，应用结果见任务进度。':'Settings submitted. Check task progress for the result.','警告：修改设备参数将影响录制，并重启数采服务。请确认以下设置：':'Warning: changing device settings affects recording and restarts the recording service. Confirm these settings:',
'已停止读取，显示最后一次读数':'Stopped; showing the last reading','读取已结束，串口已释放（每次最多 60 秒）':'Reading ended; serial ports released (60 s maximum per session)','原始计数':'Raw count','角度':'Angle','弧度':'Radians','读取时间 ':'Read at ','正在读取…':'Reading…','100 Hz 目标 / 实测 ':'100 Hz target / measured ',' Hz · 剩余 ':' Hz · remaining ',
'设备忙，请稍后抓图':'Device busy; capture later.','正在采集四路触觉并检测盖板…':'Capturing four tactile cameras and checking covers…','Ego 正在短录、拉取和截帧…':'Recording Ego, retrieving video and extracting frames…','正在获取相机帧…':'Capturing camera frames…','抓图等待超时，请检查设备':'Capture timed out; check the device','部分相机获取失败':'Some camera captures failed','获取完成':'Capture complete','获取失败':'Capture failed','本次检测完成；请查看各传感器结论':'Check complete; see each sensor result',
'左触觉传感器':'Left tactile sensor','右触觉传感器':'Right tactile sensor','触觉传感器':'Tactile sensor','传感器已更换，请重新取图':'Sensor replaced; capture again','状态未知 / 未连接':'Unknown / disconnected','无损坏告警':'No damage warning','盖板损坏告警':'Cover damage warning','未连接 / 序列号未知':'Disconnected / unknown serial','黄灯：触觉损坏告警':'Yellow: tactile damage warning','当前告警状态（连续检测历史）':'Current warning (continuous detection history)','传感器 SN：':'Sensor SN: ','当前图':'Current image','本次盖板损坏检测结果':'Cover check results','短期基线':'Short-term baseline','长期基线':'Long-term baseline','疑似盖板损坏':'Suspected cover damage','基线不匹配，请人工检查':'Baseline mismatch; inspect manually','尚未建立基线':'No baseline yet','正常':'Normal','检测状态图':'Detection status image','重置短期基线':'Reset short-term baseline','重置长期基线':'Reset long-term baseline','重置长短期基线':'Reset both baselines','设备忙，请稍后操作':'Device busy; try later',
'。请确认上方当前图中的盖板完好、无接触且无遮挡。此操作将使用该图替换基线，并清除对应检测历史与告警。':'. Confirm the cover in the image is intact, untouched and unobstructed. This replaces the baseline with this image and clears its detection history and warnings.',
'已完成，请重新运行盖板损坏检测查看更新结果':' complete. Run Cover damage check again to see updated results.',
'启动生产压测？A 组 51 轮，B 组 11 轮，约 2 小时 57 分 40 秒，另加启动和收尾时间。关闭网页或拔网线仍继续运行。':'Start production testing? A: 51 rounds, B: 11 rounds. About 2 h 57 min 40 s plus startup and finalization. The test continues after closing the page or unplugging Ethernet.',
'取消生产压测并停止当前录制？结果将标记为已取消。':'Cancel production testing and stop its current recording? The result will be marked cancelled.','正在取消，等待录制收尾':'Cancelling; waiting for recording finalization',
'状态：':'Status: ','剩余约 ':'Estimated remaining ','总失败 ':'Total failures ','A 失败 ':'A failures ','B 失败 ':'B failures ',
'通过':'Passed','不通过':'Failed','已取消':'Cancelled','异常中断':'Interrupted','执行中':'Running','启动中':'Starting','收尾中':'Finalizing','停止间隔':'Pause','已结束':'Finished','成功':'Success','失败':'Failed',
'设备正在录制、下载或维护，请稍后重试':'Device recording, downloading or under maintenance; try later','设备正在录制或收尾':'Device recording or finalizing','生产压测正在执行':'Production test running','压测任务已结束或已更换':'Test finished or replaced','请求失败':'Request failed','操作失败，请获取日志':'Operation failed; download logs','文件或服务暂不可用':'File or service unavailable','数据盘未挂载':'Data disk not mounted',
'左主摄':'Left main camera','右主摄':'Right main camera','左触觉':'Left tactile','右触觉':'Right tactile','左双目':'Left stereo','右双目':'Right stereo','左夹爪固件':'Left gripper firmware','右夹爪固件':'Right gripper firmware',
'操作完成':'Operation completed','完成':'Completed','保存设备配置':'Saving settings','已接收':'Accepted','正在执行':'Running','操作失败':'Operation failed',
'确认使用 ':'Use ',' 执行':' to perform ','软件覆盖安装':'software installation','左右两侧固件升级':'firmware update on both sides','？过程中请勿断电或拔插设备。':'? Keep power and devices connected during the operation.',
'此操作会覆盖':'This overwrites the ','编码器零位，错误操作会导致测量偏差。必须使用指定标零治具。确认仅对':' encoder zero. Incorrect calibration causes measurement errors. Use the specified fixture. Proceed only on the ','侧执行？':' side?','左':'left','右':'right'
};
let uiLanguage='zh',languageOverride=false;
const sourceText=new WeakMap();
const keys=Object.keys(translations).sort((a,b)=>b.length-a.length);
const matcher=new RegExp(keys.map(k=>k.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')).join('|'),'g');
function translateText(value){return uiLanguage==='en'?value.replace(matcher,k=>translations[k]):value;}
function translateNode(node){
 if(node.nodeType===1&&node.hasAttribute('data-no-i18n')||node.parentElement?.closest('[data-no-i18n]'))return;
 if(node.nodeType===3){
  if(['SCRIPT','STYLE'].includes(node.parentElement?.tagName))return;
  let old=sourceText.get(node);
  if(!old||node.data!==old.output)old={source:node.data};
  old.output=translateText(old.source);sourceText.set(node,old);
  if(node.data!==old.output)node.data=old.output;
 }else if(node.nodeType===1||node.nodeType===9){for(const child of node.childNodes)translateNode(child);}
}
function applyLanguage(){document.documentElement.lang=uiLanguage==='en'?'en':'zh-CN';document.querySelector('#uiLanguage').value=uiLanguage;translateNode(document);}
function setDeviceLanguage(language){if(!languageOverride){const next=language==='en'?'en':'zh';if(next!==uiLanguage){uiLanguage=next;applyLanguage();}}}
new MutationObserver(records=>{for(const record of records){if(record.type==='characterData')translateNode(record.target);else for(const node of record.addedNodes)translateNode(node);}}).observe(document.documentElement,{subtree:true,childList:true,characterData:true});
document.querySelector('#uiLanguage').onchange=e=>{languageOverride=true;uiLanguage=e.target.value;applyLanguage();};
