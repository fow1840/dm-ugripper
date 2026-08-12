# Graph Report - ugripper-1  (2026-08-04)

## Corpus Check
- 379 files · ~311,703 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 7682 nodes · 14828 edges · 466 communities (424 shown, 42 thin omitted)
- Extraction: 91% EXTRACTED · 9% INFERRED · 0% AMBIGUOUS · INFERRED: 1355 edges (avg confidence: 0.79)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `6a6a6981`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- RecordRuntime
- record_runtime.cc
- record_runtime.cc
- runtime_process.cc
- Entries
- initialize
- empty
- initialize
- initialize
- CameraConfig
- runtime_process.cc
- camera_domain.cc
- GripperHmiDriver
- camera_recorder.cc
- Changelog
- empty
- string
- v2_serial_probe.py
- StereoSessionClient
- MainCameraRecorder
- v2_episode_video_drop_report.py
- tactile_damage_check.py
- FaysRecorder
- EpisodeValidator
- fix_video_pts_v2.py
- AudioPlayer
- run_fays_stereo_daemon.sh
- ego_recording_worker.py
- StereoCameraTrack
- main_camera_xu_tool.cpp
- runtime_domain.cc
- AtrakImage
- StereoSessionRecorder
- Runtime Paths Baseline
- ShellCameraRecorder
- gripper_hmi_driver.cc
- sensor_recorder_gap_test.py
- CamTsQueue
- fays_atrak_types.h
- RecordingHarness
- .run
- .run
- gripper_hmi_toolkit.py
- hmi_sn_batch_writer.py
- GripperHmiProtocol
- HealthFault
- scan_main_camera_mkv_issues.py
- main
- SensorRecorder/main.cc
- record.cpp
- AudioCoordinator
- Ugripper Refactor Plan
- RuntimeError
- GripperHmiClient
- ego_recording_worker.py
- Ugripper V2
- currentSteadyMs
- pulse_audio_utils.py
- encoder_driver.cc
- UGripper 夹爪掉线复现测试
- gripper_hmi_protocol.cc
- runtime_domain.cc
- Ugripper Current Status Overview
- json
- usb_auto_update.sh
- gripper_hmi_driver.cc
- AudioPlayer
- batch_validate_episodes.py
- camera_recorder.cc
- Ugripper Refactor And Standalone Integration Plan
- TEST
- .open
- SerialEncoder
- StereoWarmupCapture
- record.cpp
- generate_gripper_calibration_bin.py
- bsp_crc.cc
- array
- types.hpp
- gripper_hmi_protocol.cc
- check_video_windows.py
- todo_lib.py
- writeCalibrationData
- ProcessSupervisor
- test_imu_batch_timestamp.cc
- PPMain And Ugripper Camera Refactor Reference
- main.cc
- RecordRuntimeOptions
- board_repro_log.sh
- main
- PwmChannel
- EncoderDriver
- TopicStats
- Service Map Baseline
- EgoRecordingWorkerTest
- EncoderRuntime
- summarize_episode_reports.py
- summarize_ugripper_rollout.py
- board_hmi_record_start_matrix.py
- types.hpp
- CameraRecorder
- scan_main_camera_mkv_issues.py
- usb_audio_utils.py
- 实施规则
- IntervalTree
- isConnected
- internal.hpp
- hws
- parse_args
- gripper_hmi_calibration_data.h
- Config Entrypoints Baseline
- TEST
- Status
- ReadJobQueue
- main_camera_xu_tool.cpp
- TEST
- PPMain And Ugripper Process/Thread Refactor Reference
- main
- FileLogger
- isConnected
- `pp_main` 合并后 `ugripper` 正式测试方案
- fifo_utils.cc
- readBytesLocked
- HmiController
- run_240_soak_safe.sh
- 可选 updater 包打包
- 9. 执行方式
- TEST
- usb_headset_volume_keys.py
- Control Channel Ledger Baseline
- UGripper 夹爪传感器掉线复现与日志采集 SOP
- PPMain And Ugripper Test-Oriented Refactor Reference
- test_camera_command_builder.cc
- parse_raw_pair_to_payload
- run_ugripper_calib_roundtrip_smoke.sh
- TestOptions
- RecordingOrchestrator
- PPMain And Ugripper Process/Thread Refactor Reference
- TEST
- TEST
- gripper_hmi_driver_logic.h
- test_camera_config.cc
- FaysDataLogger
- TEST
- TEST
- mcap_viewer.py
- build_runtime_venv.sh
- ValidateEpisodeProfileTest
- encoder_driver.cc
- 使用方式
- 27. Phase 1 头文件草案
- import_camera_calibration.sh
- MCAP CMake Builder
- string
- `pp_main` 合并后 `ugripper` 正式测试方案
- 4.2 `SensorRecorder`
- build_deb.sh
- TEST
- CustomizedLogFormatter
- VideoFrameQueue
- HealthMonitorOptions
- board_test_common.sh
- run_calibration.sh
- TEST
- AtrakGNSS
- EncoderData
- Naming And YAML Contract
- EnvUtilsTest
- Standalone Integration Inputs Baseline
- 需要固化的环境修改
- Ugripper 重构后架构设计与当前实现
- Repo Architecture Draft
- Ugripper Refactor And Standalone Integration Plan
- 5. 第一阶段：Ugripper Internal Refactor
- Deploy / Script Boundary Baseline
- WriteCameraRecorderFaultJson
- UMI Calibration Read/Write Protocol
- readBytesLocked
- HealthMonitor
- TEST_F
- Options
- GripperCalibrationDataV1
- gripperStateIndexForSide
- SubprocessHandle
- Ugripper V2 Overview
- 8. 灯效、音频与关键路径
- silenceBeep
- postinst
- UgripperRuntime/audio/usb_audio_utils.py
- HmiHealthSnapshot
- 6. 第二阶段：Standalone Integration
- MainCameraPtsResult
- test-ugripper-rollout
- todo-hub
- main
- error_code.h
- HmiFeedbackPattern
- TactileFrameMetrics
- LoggingV3Test
- safe_ntp_sync.sh
- 7.4 timing 模型约定
- 2. 重构原则
- Phase 7：脚本与 Python 收口
- 10. 常用检查与排障入口
- read_text
- read_text
- check_sensor_mcap.cc
- SerialLoopbackTest
- MotionAlertMessage
- AtrakIntrinsics
- AtrakObject2D
- GripperHmiSnapshot
- main
- Test Layout
- 5. 推荐的类设计
- 8.3 Phase 1 最小头文件草案
- Package Contents Baseline
- Runtime Paths Baseline
- fays_tail_imu_check.py
- setup_arm_build_env.sh
- BufferedFifoLineReader
- currentSteadyMs
- RecordingOrchestratorOptions
- camera_test.sh
- PPMain And Ugripper Naming/YAML Refactor Reference
- 4. 建议采用的目标规范
- 4.3 `Gripper` / `HMI`
- Reference-Aligned Refactor Next Steps After A3
- Ugripper Debug Playbook
- .FaysRecorder
- 1.2 Block 内字段映射
- 主摄标定数据结构说明
- run_record.sh
- run_fays_record.sh
- FaysMcapSummary
- board_ch9344_interference_matrix.sh
- 5. 推荐的 YAML 重构方式
- 21.1 配置类别
- 24. 并入 `pp_main` 前的规范对齐清单
- 25. 函数级迁移表
- 6.1 目标职责切分
- 2. 合并后的总原则
- 4. A3 之后的保守执行步骤
- 8. Step 9 之后、Stage B 之前的新增收口项
- 3.3 当前主要问题
- Phase 4：路径台账与路径封装
- mount_data_disk.sh
- board_encoder_cmd_ack_latency.py
- add-feature
- ProcessSpec
- ProcessStatus
- log_fmtter.cc
- GripperResidualsBlock
- crc32.hpp
- button_action_router.cc
- ButtonChordDefinition
- prepare_240_repro.sh
- 18. `ProcessSupervisor` 合同
- 2. `pp_main` 的进程/线程管理模式
- 3. `ugripper` 当前的进程/线程管理模式
- check_gripper_ack_log.py
- bump_release_versions.sh
- debug-ugripper
- git-commit
- test-new-feature
- Runtime Protocol
- log_string.h
- TEST
- ugripper_log_writer.py
- board_encoder_to_hmi_interference_matrix.sh
- PPMain And Ugripper Camera Refactor Reference
- 3. `ugripper` 当前在命名和 YAML 上的核心问题
- 6. 建议的重构顺序
- 15.2 Phase 1 结束后的目标形态
- 2.1 当前已有测试模块
- 7. 分阶段落地路线
- 10. 推荐执行顺序
- Ugripper Refactor Plan
- 7. 测试与门禁策略
- 9. 配置、安装与 U 盘流程
- write_source_manifest.sh
- TODO Schema
- WriteCalibrationJsonFromHandle
- test_audio_coordinator.cc
- test_stereo_session_client.cc
- ugripper_shell_common.sh
- CalculateCrc16Modbus
- GripperExtrinsicsBlock
- ShutdownRequestPort
- board_gripper_hmi_active_check.sh
- 3. `ugripper` 当前相机代码的主要问题
- 23.2 推荐测试分层
- 28. 建议的拆分 PR 顺序
- 2. `pp_main/test` 里值得复用的设计
- 3. `ugripper` 当前阻碍测试的核心问题
- 7. 推荐的测试分层
- 3. 重构原则
- 7. 重构阶段
- Phase 2：建立最小 `src/utils`
- boot_check_install.sh
- Version Test Focus
- validate-episode-data
- `pp_main` 合并后 `ugripper` 测试结构与当前结果简表
- 5. 响应约定
- RunControlLoop
- install_deb_to_arm_target.sh
- open_foxglove_episode.sh
- verify_arm_board_install.sh
- FaysImuSample
- GripperCalibrationHeader
- GripperImuBlock
- FourButtonSnapshot
- TactileCalibrationTarget
- board_gripper_hmi_link_stress.sh
- 4. 重构目标：不是统一成一个基类，而是拆成三层
- 19. `RecordingOrchestrator` 状态机
- 5. 针对已知问题的专项补测清单
- 6. 最终目标
- 4.2 日志规则
- 4.4 utils 规则
- Phase 5：收口低风险 helper
- Phase 6：Camera 抽象收口
- monitor_network.sh
- auto_release_deb.sh
- auto-release-deb
- Episode 校验清单
- Singleton
- Ugripper 错误码对照表
- TODO List
- GstRecorder
- GripperRgbCameraBlock
- board_camera_stress.sh
- board_gripper_hmi_fault_soak.sh
- board_gripper_hmi_log_check.sh
- board_hmi_high_rate_soak.sh
- board_hmi_self_timeout_soak.sh
- board_hmi_strict_reply_soak.sh
- board_tactile_ffmpeg_parallel_soak.sh
- board_tactile_ffmpeg_streamon_soak.sh
- 10. 哪些设计可以直接借鉴，哪些不能直接照搬
- 9. 建议的迁移顺序
- 2. `pp_main` 当前可复用的规范基线
- 11. 推荐迁移阶段
- 12. 重构判断标准
- 20. shell / systemd / runtime 边界
- 6. 录制生命周期
- network_iface_lib.sh
- Q: 以主摄链路为例，讲解从采集线程拉起到文件构建构的过程
- Q: 当前的事件是基于已有的日志，以及项目实际情况做的全覆盖的事件类型吗
- Q: 基于这个项目给我介绍一下usb总线，我没学过
- Q: 我现在同步在执行错误码修改的phase3,4,该日志进行修改会影响吗
- Q: 开始分析status log,这个文件的目的是给使用数采结果的算法员，能够获得该episode的软硬件环境，保留核心信息
- Q: 介绍一下，一次episode结束会产生的日志
- Q: 该项目debug是用gdb吗？
- Q: 更新status的指责：用于给另一个进程的通信，比如一个手机程序要实时获取rk采集状态，需要读取这个status.json status的内容是各个硬件的状态，比如世界相机，fays等，目的是如果报错了能快速获取错误位置，再次重构status的内容
- Q: rk板里面/dev/shm/ugripper都是什么文件
- Q: 这是我之前和ai的对话，基于当前本地你可以读取的这个项目，继续回答我的问题
- Q: 是不是走其中一个硬件的完整数据链路和开发过程？
- Q: 但是这些完整链路是不是当前项目已经实现了？
- Q: 目前我还在进行日志重构和错误码重构，给我这三个优化项目的执行顺序
- Q: 基于我的简历模板，如何把 Ugripper 项目写成符合实习生贡献尺度、突出完整数据链路、架构优化和性能提升的嵌入式 Linux 项目经历？
- Q: 基于 Ugripper 串口 encoder 数据链路扫盲并准备面试问题
- Q: Ugripper encoder 串口链路应该优化哪些数据指标，对应方案有哪些？
- Q: 目前phase4已经全部完成，检查日志更新条件是否成熟
- Q: 关于错误码在日志的展现形式：以目前已经重构好的错误码为准；进入R1需要创建的两个文档作用是什么
- Q: 现有项目生成的4个mcap文件，以sensor_left.mcap和fays_data_left.mcap为典型，这两个mcap文件结构如何定义，有什么字段，每个字段包含什么数据，数据怎么来，解释数据内容
- Q: 解释sensor_left.mcap的message中每个字段的含义
- Q: mcap文件生成之后，256 KiB Chunk、LZ4压缩、Schema/Channel记录、Footer、不生成Message Index是什么，开与不开有什么影响
- Q: chunk可以设置压缩前大小，现在是256k；LZ4是压缩协议还是压缩选项？还有别的选择吗
- Q: 现在说要我做力传感器的接入，数据链路构建（使用现有的sensor_encoder链路，用串口通信+CRC校验），以及传感器数据的标定（力的大小）
- Q: 如果要是各个传感器数据同时使用训练，mcap的时间戳需要对齐吗/项目里有对齐吗
- Q: 解析sensor_code链路，如果可以，从底层寄存器数据读取开始，到串口数据传输的参数配置，板端解包过程，数据读取过程，打包为mcap的过程（更正：sensor_encoder）
- Q: pp-main docker里面是否有一个”pp main“ CPP框架
- Q: Ugripper在容器内编译，需要使用的pp_main框架呢
- Q: 检查 /home/dm/Doc/ugripper_runtime_status_design_v1.md 中 status.log 方案当前构建进入到哪一步
- Q: 是否可以找到完整的PP_main仓库
- Q: 当前docker是怎么构建的？里面的pp_main组件是怎么下载的？
- Q: README.md里面不是说交叉编译容器需要 /home/dm/proj/pp-main/standalone/UGripper 文件夹吗？本地是否存在
- Q: 为什么当时构建的时候没有按照README走
- Q: 如果不在 pp_main 上构建，Harbor 镜像到 pp-arm-dev 容器、挂载 ugripper-1、使用 ARM toolchain 编译并打 deb 的过程写在哪里？
- Q: pp-arm-dev 这个名称在哪里沿用的？
- Q: ego和umi在项目里是什么意思
- Q: /home/dm/Code/ugripper-1/这里是具体的蔬菜项目，比较成熟，已有传感器链路，可以参考这个吗
- Q: 分析原有日志的问题，以及新日志的优越性，这段总结是否正确
- Q: 有几个文件的当前功能分支的版本对比develop-aci修改过大，分析原因： run_fays_stereo_daemon.sh
- Q: log_event和log_adapter需要被高度复用，目前功能分支 的head具有大量的代码冗余，分析原因以及优化方案，优化方向就是代码的复用，包括C++类的复用等方式
- Q: 1. audio的问题不管 2. 这样会改什么文件？新增什么文件？能够实现降低代码冗余度的效果吗
- Q: 基于当前已经做好的串口传输编码器数据链路作为成果 /home/dm/Code/ugripper-1/，构建简历内容
- Q: 这几个才减少十几行？
- Q: 之前说的将日志业务代码剥离出主业务文件没有做吗
- Q: 所以你是没有按照执行文件走吗
- Q: 所以应该回撤到阶段B,执行阶段B到E是吗
- Q: runtime_domain.cc 的 sensor_args 是什么含义
- Q: 前面精简版和详细版的简历都没有数据成果，无法给简历成果做支撑，如果要添加数据作为实习成果，应该怎么测试
- Q: 编码器链路的worker做什么
- build_arm_deb_in_pp_arm_dev.sh
- ugripper_restore_usb
- ScanMainCameraMkvIssuesTest
- board_hmi_record_start_matrix.sh script
- board_service_episode_loop_check.sh
- board_service_integration_check.sh
- camera_crash_capture.sh
- 4. 两种模式的根本差异
- 5. 后续重构的总原则
- 7. 哪些边界建议保留多进程
- 8. 从 `pp_main` 可直接复用什么
- collect_debug_context.sh
- install-arm-deb
- <旧版本> → <新版本> 测试关注点
- collect-version-evidence.sh
- allocate_worktree.sh
- ensure_main_camera_packet_size_once.sh
- TEST
- Q: 原来 Ugripper 是否依赖 pp_main，当前交叉编译容器是否把 pp_main 组件单独放在 Harbor？
- Q: Harbor 中的工具链和组件是否大量采用 pp_main 的内容？
- Q: pp_main 具体在哪里影响了 Ugripper？通过什么方式影响？
- Q: 开发过程中的项目框架、线程池、进程管理是否由 pp_main 影响 Ugripper？
- legacy_flat_valid.yaml
- prerm
- restore_service
- board_sensor_smoke.sh
- validate
- test_ch9344_symlink_name.sh
- pull_240_repro_logs.sh
- usb_updater_build.sh
- README.md
- trigger_shutdown.sh
- batch_calibrate.sh
- stereo_session_port.h
- ugripper_build_variant.sh
- adb
- board_service_restart_check.sh
- ptp_monitor.sh
- usb_updater_transition_build.sh
- ensure_monitor.sh
- launch_agent_session.sh
- monitor_tasks.sh
- relay_command.sh
- report_progress.sh
- Fays VIKit SDK Provenance
- postrm
- ASR README
- ugripper_ch9344_symlink_name.sh
- HmiController::updateDebouncedButton
- testVideoPipe.sh
- validate_episode_data.sh
- .Make
- camera_config library
- chest_cam_main
- left_cam_main
- left_tcam_l
- right_cam_main
- right_tcam_l
- /etc/ugripper/config/calibration/calibration.json
- config/fakeCamCalib.json
- /mnt/data_disk/calibration.txt
- USB config.txt
- episode_manager

## God Nodes (most connected - your core abstractions)
1. `RecordRuntime` - 258 edges
2. `empty()` - 158 edges
3. `Entries` - 136 edges
4. `GripperHmiDriver` - 116 edges
5. `MainCameraRecorder` - 93 edges
6. `FaysRecorder` - 84 edges
7. `CameraConfig` - 81 edges
8. `StereoSessionRecorder` - 76 edges
9. `main()` - 60 edges
10. `RecordingHarness` - 60 edges

## Surprising Connections (you probably didn't know these)
- `docs/ppmain-ugripper-test-summary.md` --semantically_similar_to--> `pp_main merged test summary`  [INFERRED] [semantically similar]
  README.md → docs/ppmain-ugripper-test-summary.md
- `TEST()` --calls--> `Poll`  [INFERRED]
  test/src/record_runtime/test_runtime_health.cc → standalone/UgripperRuntime/include/record_runtime/health_monitor.h
- `TEST()` --calls--> `Reset`  [INFERRED]
  test/src/record_runtime/test_button_logic.cc → standalone/UgripperRuntime/include/record_runtime/hmi_controller.h
- `pollRecordControlPipe()` --calls--> `IsOpen()`  [INFERRED]
  standalone/UgripperRuntime/record_runtime.cc → src/utils/src/fifo_utils.cc
- `Ugripper V2` --conceptually_related_to--> `AudioCommandPort`  [AMBIGUOUS]
  README.md → docs/archive/2026-refactor-history/standalone-merge-plan.md

## Import Cycles
- None detected.

## Communities (466 total, 42 thin omitted)

### Community 0 - "RecordRuntime"
Cohesion: 0.01
Nodes (250): BackgroundTactileValidationResult, EpisodeManager, HmiLedController, MainCameraRuntimeCache, shared_ptr, array, atomic, GripperPanelManager (+242 more)

### Community 1 - "record_runtime.cc"
Cohesion: 0.11
Nodes (19): byte, condition_variable, deque, vector, PendingWriteQueue, cv, label, maxObservedBacklog (+11 more)

### Community 2 - "record_runtime.cc"
Cohesion: 0.02
Nodes (164): HealthSnapshot, kYuzhouPayloadSize, MainCameraRuntimeState, to_string(), Statistics(), activeCriticalDevicePaths(), activeEpisodeVideoArtifacts(), addVersionPrefix() (+156 more)

### Community 3 - "runtime_process.cc"
Cohesion: 0.08
Nodes (23): future, RefreshStatus, pid_t, vector, WorkerName, ChildFdScanLimit(), IsInheritedFd(), JoinArguments() (+15 more)

### Community 4 - "Entries"
Cohesion: 0.01
Nodes (136): 2026-04-03 - establish-refactor-workflow-docs, 2026-04-08 - add-camera-abstraction-and-testability-requirements, 2026-04-08 - phase-a1-baseline-freeze, 2026-04-08 - phase-a2-bootstrap-minimal-utils, 2026-04-08 - phase-a3-unify-cpp-logging, 2026-04-08 - refine-standalone-merge-test-strategy-and-step-reminders, 2026-04-08 - unify-cpp-standard-to-cpp20, 2026-04-09 - architecture-report-documentation (+128 more)

### Community 5 - "initialize"
Cohesion: 0.12
Nodes (16): 7.1 构建, 7.2.1 推荐出包流程, 7.2.2 `build_deb.sh` 当前实际流程, 7.2.3 `build_deb.sh` 的两种运行方式, 7.2.4 当前手动出包方法, 7.2.5 当前 agent 需要知道的操作性结论, 7.2.6 当前 ARM 出包容器所需环境, 7.2 主包打包 (+8 more)

### Community 6 - "empty"
Cohesion: 0.07
Nodes (40): ConnectionEvent, clearEgoBindingFault, clearEgoBindingState, isEgoRequired, runCommandSync, setEgoBindingFault, boxBlurFrame(), vector (+32 more)

### Community 7 - "initialize"
Cohesion: 0.17
Nodes (14): NowNsFn, BatchTimestampSmoothingState, has_last_assigned_timestamp, label, last_assigned_timestamp_ns, nominal_period_ns, smoothed_batch_count, smoothed_frame_count (+6 more)

### Community 8 - "initialize"
Cohesion: 0.05
Nodes (101): reset(), isStereoStartupWaiting(), applyIdleState(), applyTactileValidationFindings(), checkRecorderProcesses(), clearEgoBindingFault(), clearEgoBindingState(), clearPendingPhysicalRecordShortPress() (+93 more)

### Community 9 - "CameraConfig"
Cohesion: 0.06
Nodes (36): set, IsSelectedCamera(), IsStereoCamera(), ShouldManageInStereoDaemon(), CameraConfig, capture_height, capture_width, device (+28 more)

### Community 10 - "runtime_process.cc"
Cohesion: 0.09
Nodes (28): ReadyFn, runtime::ShutdownRequestPort, AudioCoordinator::SetRecoveryCommand(), BuildStereoSessionJson(), json, path, runtime::StereoSessionPort, string (+20 more)

### Community 11 - "camera_domain.cc"
Cohesion: 0.21
Nodes (30): Node, CameraKind, CameraRecordMode, CameraRole, CameraSide, path, string, vector (+22 more)

### Community 12 - "GripperHmiDriver"
Cohesion: 0.02
Nodes (91): function, GripperHmiDriver::connect(), GripperHmiDriver::GripperHmiDriver(), GripperHmiDriver::runExclusiveCommand(), GripperHmiDriver::setBeepEnabled(), GripperHmiDriver::silenceBeep(), GripperHmiDriver, activeBeepState_ (+83 more)

### Community 13 - "camera_recorder.cc"
Cohesion: 0.06
Nodes (62): AllExpectedTimingOffsetsAvailable(), AppendNalTypeSummary(), BootTimeOffsetUs(), CameraRecorderFaultPathForEpisode(), CameraRecorderManager::AllExpectedTimingOffsetsAvailable(), CameraRecorderManager::WriteInfoJson(), CameraRecordMode, json (+54 more)

### Community 14 - "Changelog"
Cohesion: 0.23
Nodes (91): Changelog, Changelog, Changelog, Changelog, Changelog, Changelog, Changelog, Changelog (+83 more)

### Community 15 - "empty"
Cohesion: 0.06
Nodes (76): error_code, flush(), empty(), Exists(), connect(), resetConnectFailureLogLocked(), resolveSerialPortPath(), Message() (+68 more)

### Community 16 - "string"
Cohesion: 0.06
Nodes (7): string, vector, camera_recorder(), set, optional, array, thread

### Community 17 - "v2_serial_probe.py"
Cohesion: 0.11
Nodes (32): build_im648_command(), build_port_info(), chain_text(), choose_encoder_port(), crc16_modbus(), decode_encoder_position(), encode_encoder_read(), encode_encoder_write() (+24 more)

### Community 18 - "StereoSessionClient"
Cohesion: 0.06
Nodes (37): DirectSideFifosStopAndFinalizeWithoutTopControlPipe, ReportsFinalizeErrorFromStatusFile, ReportsFinalizeErrorWhileDaemonIsStillFinalizing, NowMsFn, unique_ptr, StereoSessionClient, active_episode_dir_, active_start_system_time_us_ (+29 more)

### Community 19 - "MainCameraRecorder"
Cohesion: 0.04
Nodes (49): duration, GstBus, GstElement, GstMessage, QueueLevels, milliseconds, GstMessageSourceName(), MainCameraRecorder (+41 more)

### Community 20 - "v2_episode_video_drop_report.py"
Cohesion: 0.12
Nodes (36): analyze_device_root(), analyze_episode_stream(), analyze_packet_sequence(), build_alert_episode_rows(), build_alert_episode_stream_rows(), build_device_summary_rows(), build_episode_stream_rows(), build_episode_summary_rows() (+28 more)

### Community 21 - "tactile_damage_check.py"
Cohesion: 0.12
Nodes (36): _add_data_root(), add_label(), build_report_root(), compute_damage_metrics(), detect_latest_root(), detect_layout(), device_label_from_metadata(), discover_data_roots() (+28 more)

### Community 22 - "FaysRecorder"
Cohesion: 0.04
Nodes (48): FaysRecorder, calibrationDumped_, calibrationJsonPath_, camTsQueue_, configPath_, hasImuTimeOffset_, imuGapCount_, imuQueue_ (+40 more)

### Community 23 - "EpisodeValidator"
Cohesion: 0.08
Nodes (29): build_parser(), ensure_ffprobe(), EpisodeValidator, FaysMcapStats, Finding, list_episode_dirs(), main(), ns_to_ms() (+21 more)

### Community 24 - "fix_video_pts_v2.py"
Cohesion: 0.13
Nodes (32): analyze_video(), _build_default_run_dir(), _build_issue_tags(), build_report_paths(), _collect_frame_stats(), _copy_file(), _estimate_effective_fps(), _format_float() (+24 more)

### Community 25 - "AudioPlayer"
Cohesion: 0.07
Nodes (40): audio_log(), read_env_file_value(), AudioPlayer, _find_event_device(), _handle_volume_key(), main(), BytesIO, Path (+32 more)

### Community 26 - "run_fays_stereo_daemon.sh"
Cohesion: 0.08
Nodes (51): apply_side_maintain_result(), calibration_serial(), check_runtime_health_during_finalize(), check_side_device_paths(), check_side_working_state(), check_side_working_state_reason(), cleanup(), cleanup_stale_fays_recorders() (+43 more)

### Community 27 - "ego_recording_worker.py"
Cohesion: 0.17
Nodes (35): best_offset_ms(), binding_state_lock(), cleanup_status(), compact_status(), compact_time_sync(), ego_codec_for_camera_codec(), final_name(), find_valid_box_marker() (+27 more)

### Community 28 - "StereoCameraTrack"
Cohesion: 0.09
Nodes (18): CameraRecorderManager::MonitorUntilStop(), CameraRecorderManager::StopAll(), StereoCameraTrack, config_, frame_drop_modulo_, last_error_, last_start_attempt_system_time_us_, options_ (+10 more)

### Community 29 - "main_camera_xu_tool.cpp"
Cohesion: 0.06
Nodes (48): BandwidthMode, kXuPacketSize, MainCameraCalibrationDataV1, header, mainCamera, reserved, MainCameraCalibrationHeader, dataFormatVersion (+40 more)

### Community 30 - "runtime_domain.cc"
Cohesion: 0.15
Nodes (37): initializer_list, LogFn, AddErrorType(), AddErrorTypes(), AppendHmiPortActivity(), ApplyFailureLedState(), CallLog(), CallPerfLog() (+29 more)

### Community 31 - "AtrakImage"
Cohesion: 0.17
Nodes (13): AtrakAprilTag, corners, id, pos, pose_available, quat, tag_family, AtrakAprilTagDets (+5 more)

### Community 32 - "StereoSessionRecorder"
Cohesion: 0.05
Nodes (36): QueuedFrame, condition_variable, deque, StereoSessionRecorder, command_, config_, exit_code_, failure_ (+28 more)

### Community 33 - "Runtime Paths Baseline"
Cohesion: 0.16
Nodes (15): BuildMainCameraCommand(), BuildStereoHybridCameraCommand(), BuildStereoSessionCommand(), BuildStereoSessionVideoFilter(), OutputPath(), ShellQuote(), StereoSessionFps(), WrapExecEnvCommand() (+7 more)

### Community 34 - "ShellCameraRecorder"
Cohesion: 0.05
Nodes (24): GObject, optional, pid_t, ReadGObjectUintProperty(), ShellCameraRecorder, BuildCommand, command_, config_ (+16 more)

### Community 35 - "gripper_hmi_driver.cc"
Cohesion: 0.19
Nodes (9): StartAudioPlayer, AudioCoordinator::MaintainAudioPlayer(), AudioCoordinator::SendCommand(), AudioCoordinator::StartAudioPlayer(), AudioCoordinator::StopAudioPlayer(), runtime::AudioCommandPort, FileAudioCommandPort, command_path_ (+1 more)

### Community 36 - "sensor_recorder_gap_test.py"
Cohesion: 0.16
Nodes (27): ndarray, assert_output_has_mcaps(), build_topic_stats(), collect_gap_events(), create_output_dir(), ensure_mcap_dependency(), GapEvent, iter_mcap_messages() (+19 more)

### Community 37 - "CamTsQueue"
Cohesion: 0.06
Nodes (34): CamTsQueue, cv_, mtx_, nextBacklogWarnSize_, queue_, stopped_, CamTsQueuedSample, faysTsNs (+26 more)

### Community 38 - "fays_atrak_types.h"
Cohesion: 0.10
Nodes (23): AtrakCalibrationParam, cameras, imu, AtrakCamChainParam, cameras, downsize_ratio, num_of_cams, AtrakCamParam (+15 more)

### Community 39 - "RecordingHarness"
Cohesion: 0.04
Nodes (54): map, RuntimeLedState, vector, WorkerName, RecordingHarness, attach_ok, audio_commands, camera_bin_exists (+46 more)

### Community 40 - ".run"
Cohesion: 0.10
Nodes (28): enhance_audio(), maintain_audio_limit_by_timestamp(), Path, 启动进程并创建新进程组，以便 kill 时能杀掉子进程, run_asr(), setup_gpio_high(), start_process(), _format_number() (+20 more)

### Community 41 - ".run"
Cohesion: 0.17
Nodes (20): Adb, detect_ego(), detect_egos(), get_props(), is_ego_device(), list_devices(), list_temp_episodes(), CompletedProcess (+12 more)

### Community 42 - "gripper_hmi_toolkit.py"
Cohesion: 0.14
Nodes (10): BuildsStableServiceStatusJson, ParsesStereoControlCommand, RejectsNonObjectControlJson, StereoControlJsonTest, TEST(), BuildsStableServiceStatusJson, ParsesStereoControlCommand, RejectsNonObjectControlJson (+2 more)

### Community 43 - "hmi_sn_batch_writer.py"
Cohesion: 0.17
Nodes (12): ATRAK_TAG_FAMILY, AtrakAprilTagTrackConfig, seq, tag_family, tag_size, timestamp, AtrakPose, confidence (+4 more)

### Community 44 - "GripperHmiProtocol"
Cohesion: 0.05
Nodes (66): GripperCalibrationDataV1, string, describeStatusFrame(), formatHexByte(), GripperHmiDriver::abortCalibrationWriteStateLocked(), GripperHmiDriver::logIoSummaryLocked(), GripperHmiDriver::readCalibrationData(), GripperHmiDriver::readCalibrationDataLocked() (+58 more)

### Community 45 - "HealthFault"
Cohesion: 0.07
Nodes (41): RestoreUsbTriggerContext, confirmRestoreUsbTrigger, ledStateName, logRestoreUsbRequested, logRestoreUsbSkipOnce, makeLedEffect, prepareDataDiskForRestoreUsb, shouldDeferRestoreUsbForSide (+33 more)

### Community 46 - "scan_main_camera_mkv_issues.py"
Cohesion: 0.16
Nodes (30): align_shared_events(), analyze_file(), analyze_packets(), auto_jobs(), build_pair_summaries(), build_parser(), classify_status(), DecodeSummary (+22 more)

### Community 47 - "main"
Cohesion: 0.14
Nodes (29): runtime_error, had_failure(), InstallSignalHandlers(), StartAll(), CameraConfigurationError, Options, path, main() (+21 more)

### Community 48 - "SensorRecorder/main.cc"
Cohesion: 0.10
Nodes (31): condition_variable, buildEncoderSchema(), buildQueuedMessage(), atomic, EncoderData, path, string, connectEncoderWithFallback() (+23 more)

### Community 49 - "record.cpp"
Cohesion: 0.14
Nodes (28): ChildFileDescriptorLimit(), CleanupSdkTempConfig(), CloseChildFileDescriptors(), string, CreateStableFaysHandle(), DumpCalibrationJson(), EnsureControlFifo(), ErrnoToString() (+20 more)

### Community 50 - "AudioCoordinator"
Cohesion: 0.10
Nodes (22): AudioCoordinator, command_port_, last_start_attempt_ms_, MaintainAudioPlayer, now_ms_fn_, options_, recovery_command_, SendCommand (+14 more)

### Community 51 - "Ugripper Refactor Plan"
Cohesion: 0.18
Nodes (11): AtrakDepthImage, bytes, channel, data, device_id, encoding, height, seq (+3 more)

### Community 52 - "RuntimeError"
Cohesion: 0.12
Nodes (28): RuntimeError, format_hex_lines(), load_toolkit(), main(), Path, read_calibration_raw(), resolve_repo_root(), build_calib_chunk_frame() (+20 more)

### Community 53 - "GripperHmiClient"
Cohesion: 0.18
Nodes (11): AtrakDeviceInfo, device_id, device_ip_address, device_version, driver_version, product_name, product_serial_number, product_type (+3 more)

### Community 54 - "ego_recording_worker.py"
Cohesion: 0.18
Nodes (11): AtrakImage, bytes, channel, data, device_id, encoding, height, seq (+3 more)

### Community 55 - "Ugripper V2"
Cohesion: 0.18
Nodes (11): string, vector, StereoSessionClientOptions, control_pipe, daemon_arguments, daemon_stop_timeout_ms, finalize_wait_poll_ms, left_control_fifo (+3 more)

### Community 56 - "currentSteadyMs"
Cohesion: 0.06
Nodes (44): currentSteadyMs, detectCameraRecorderStartupFailureForEpisode, ensureEpisodeManagerInitialized, handleLeftDualUmountAction, handleLeftSingleLongMarkFailedAction, handleShortDownAction, handleShortUpAction, resetCameraRecorderDStateTracking (+36 more)

### Community 57 - "pulse_audio_utils.py"
Cohesion: 0.08
Nodes (40): audio_log(), read_env_file_value(), AudioPlayer, _find_event_device(), _handle_volume_key(), main(), BytesIO, Path (+32 more)

### Community 58 - "encoder_driver.cc"
Cohesion: 0.44
Nodes (10): calculateCRC16(), sendConfigToEncoder(), setBaudrate(), setConfigStore(), setCurrentAsMiddle(), setCurrentAsZero(), setDeviceAddress(), setRefactory() (+2 more)

### Community 59 - "UGripper 夹爪掉线复现测试"
Cohesion: 0.14
Nodes (22): 1. 开始自动测试, 2. 常用参数, 3. 手动实时看日志, 4. 拉回日志, 5. 停止和清理, 6. 只下发脚本不测试, code:bash (./scripts/prepare_240_repro.sh), code:bash (ssh ubuntu@192.168.2.240 'bash /tmp/gripper_disconnect_repro) (+14 more)

### Community 60 - "gripper_hmi_protocol.cc"
Cohesion: 0.07
Nodes (54): BuildSetBeepCommandUsesFreeIndexAndClampsDuty, DescribeStatusCodeKeepsStableKnownStrings, GripperFrameType, GripperHmiProtocolTest, ioLoop(), isExclusiveStatusFrame(), isValidEightByteRecvFrame(), validateCalibrationReadFrame() (+46 more)

### Community 61 - "runtime_domain.cc"
Cohesion: 0.17
Nodes (12): Dependencies, EpochUsFn, SteadyMsFn, RecordingOrchestrator, CheckRecorderProcesses, dependencies_, epoch_us_fn_, options_ (+4 more)

### Community 62 - "Ugripper Current Status Overview"
Cohesion: 0.05
Nodes (47): nlohmann_json, yaml-cpp, 10. 当前维护入口, 1. 当前仓库一句话, 2.1 主实现真源, 2.2 测试真源, 2.3 部署与交付真源, 2.4 文档真源 (+39 more)

### Community 63 - "json"
Cohesion: 0.08
Nodes (27): json, map, string, StereoControlCommand, command_seq, episode_dir, recording, start_system_time_us (+19 more)

### Community 64 - "usb_auto_update.sh"
Cohesion: 0.10
Nodes (27): apply_imports_with_led_feedback(), cleanup(), clear_self_update_pending_completion(), ensure_package_install_window(), enter_upgrade_window(), install_usb_package_if_needed(), kill_tree(), leave_upgrade_window() (+19 more)

### Community 65 - "gripper_hmi_driver.cc"
Cohesion: 0.22
Nodes (9): AtrakExtraPose, cov, frame, pos, quat, seq, status, timestamp (+1 more)

### Community 66 - "AudioPlayer"
Cohesion: 0.33
Nodes (5): CameraRecorderManager::StartAll(), thread, ChildFileDescriptorLimit(), CloseChildFileDescriptors(), ConfigureManagedChildProcessGroup()

### Community 67 - "batch_validate_episodes.py"
Cohesion: 0.22
Nodes (22): build_markdown_report(), build_parser(), build_validate_command(), candidate_key(), choose_python_runner(), discover_device_roots(), EpisodeResult, find_first_issue() (+14 more)

### Community 68 - "camera_recorder.cc"
Cohesion: 0.06
Nodes (27): CameraRecorderManager::CameraRecorderManager(), CameraRecorderManager::Prepare(), Options, unique_ptr, vector, CreateRecorder(), HybridCameraRecorder, ListSortedFiles() (+19 more)

### Community 69 - "Ugripper Refactor And Standalone Integration Plan"
Cohesion: 0.22
Nodes (9): ButtonStateTracker, both_pressed_since_ms, down_long_handled, down_pressed_since_ms, dual_chord_active, dual_long_handled, shutdown_prompt_played, up_long_handled (+1 more)

### Community 70 - "TEST"
Cohesion: 0.06
Nodes (46): GripperLedEffectRendererTest, InitBlinksBlue, ParseStateTextAcceptsKnownStateAndClampsProgress, ParseStateTextAcceptsWarning, ParseStateTextAcceptsWriting, ParseStateTextRejectsUnknownState, ReadyBreathStartsBright, RenderProducesStableColorsForSimpleStates (+38 more)

### Community 71 - ".open"
Cohesion: 0.12
Nodes (27): stop_process(), ModbusSerialClient, calculate_crc16(), find_devices(), main(), Serial, 对所有编码器进行归零操作          Args:         baudrate: 波特率，默认115200, 对单个编码器进行归零          Args:         device_path: 设备路径，如 '/dev/ttyCH9344USB0' (+19 more)

### Community 72 - "SerialEncoder"
Cohesion: 0.17
Nodes (4): EncoderData, main(), SerialEncoder, SerialManager

### Community 73 - "StereoWarmupCapture"
Cohesion: 0.08
Nodes (18): FrameHandler, MmapBuffer, atomic, CurrentSystemTimeUs(), StereoWarmupCapture, buffers_, capture_thread_, config_ (+10 more)

### Community 74 - "record.cpp"
Cohesion: 0.39
Nodes (8): BuildHybridCameraCommand(), BuildVideoFilter(), CaptureHeight(), CaptureInputFormat(), CaptureWidth(), FrameDropModulo(), OutputTimingArgs(), UsesFrameDropWithPreservedPts()

### Community 75 - "generate_gripper_calibration_bin.py"
Cohesion: 0.29
Nodes (7): AtrakTempTrackConfig, cls, physical_height, physical_width, seq, template_img, timestamp

### Community 76 - "bsp_crc.cc"
Cohesion: 0.13
Nodes (12): CRC16Type, crc16_iso13239(), crc16_modbus_nibble(), Get_CRC16(), BspCrcTest, CalculatesKnownIso13239Crc, CalculatesKnownModbusCrc, TEST() (+4 more)

### Community 77 - "array"
Cohesion: 0.29
Nodes (7): ViKitDeviceInfo, camera_nums, device_model, firmware_version, imu_nums, reserved, serial_number

### Community 78 - "types.hpp"
Cohesion: 0.33
Nodes (6): 5.1 `src/utils`, 5.2 `src/gripper_hmi`, 5.3 `src/sensor_recorder`, 5.4 `src/camera_recorder`, 5.5 `src/record_runtime`, 5. 建议的模块重构方向

### Community 79 - "gripper_hmi_protocol.cc"
Cohesion: 0.33
Nodes (6): AtrakMap, device_id, point_num, pos, rgb, timestamp

### Community 80 - "check_video_windows.py"
Cohesion: 0.19
Nodes (9): count_frames(), decode_window(), evaluate_windows(), main(), parse_fraction(), probe_stream_info(), run_command(), summarize_failures() (+1 more)

### Community 81 - "todo_lib.py"
Cohesion: 0.12
Nodes (31): main(), main(), add_common_args(), cmd_show(), cmd_upsert(), main(), ArgumentParser, Namespace (+23 more)

### Community 82 - "writeCalibrationData"
Cohesion: 0.07
Nodes (48): abortCalibrationWriteStateLocked(), allocateCommandIdLocked(), array, GripperCalibrationHeader, kCalibrationChunkCount, kCalibrationPayloadSize, currentSteadyMs(), disconnect() (+40 more)

### Community 83 - "ProcessSupervisor"
Cohesion: 0.14
Nodes (17): MarksUnexpectedExit, ProcessSupervisorTest, array, ProcessSupervisor, GetStatus, IsRunning, SendSignal, Start (+9 more)

### Community 84 - "test_imu_batch_timestamp.cc"
Cohesion: 0.10
Nodes (23): byte, vector, TimestampedPayloadFrame, host_timestamp_ns, payload, byte, MultiFrameBackfillsFromLastTimestamp, MultiFrameClampsBackwardsTimestampsToMonotonicSequence (+15 more)

### Community 85 - "PPMain And Ugripper Camera Refactor Reference"
Cohesion: 0.27
Nodes (11): ugripper camera domain model, CameraDevice, CameraNode, CameraRegistry, CameraSession, ShellRecordBackend, StereoCameraNode, StereoCameraSession (+3 more)

### Community 86 - "main.cc"
Cohesion: 0.33
Nodes (6): AtrakRectifyInfo, cam_pair, P0, P1, R0, R1

### Community 87 - "RecordRuntimeOptions"
Cohesion: 0.06
Nodes (35): string, vector, RecordRuntimeOptions, audioPipe, audioPlayScript, audioReadyFile, audioRecordScript, audioTempDir (+27 more)

### Community 88 - "board_repro_log.sh"
Cohesion: 0.12
Nodes (28): auto_record_loop(), capture_command(), capture_now(), colorize_hws(), copy_if_needed(), data_disk_ready(), ensure_sudo(), episode_error_summary() (+20 more)

### Community 89 - "main"
Cohesion: 0.40
Nodes (6): BuildStereoServiceStatusJson(), BuildStereoTrackStatusJson(), json, optional, MainCameraPtsFromV4l2TimeUs(), ParseStereoControlCommand()

### Community 90 - "PwmChannel"
Cohesion: 0.26
Nodes (4): interactive_loop(), main(), PwmChannel, RgbLed

### Community 91 - "EncoderDriver"
Cohesion: 0.05
Nodes (38): ConnectStatus, sp_port, EncoderDriver::connect(), EncoderDriver::requestState(), parseReceivedData(), updateActiveStatus(), EncoderDriver, baudrate_ (+30 more)

### Community 92 - "TopicStats"
Cohesion: 0.06
Nodes (35): json, FileResult, failures, ok, path, reference_span_ns, topics, optional (+27 more)

### Community 93 - "Service Map Baseline"
Cohesion: 0.11
Nodes (16): 1. 当前本地环境说明, 2. 行为等价检查清单, 3.1 Shell 语法检查, 3.2 Python 语法检查, 3.3 最小命令探测, 3. 当前已执行的本地验证, 4. 当前不能在本地 Linux 完成的验证, 5. 后续使用方式 (+8 more)

### Community 94 - "EgoRecordingWorkerTest"
Cohesion: 0.12
Nodes (5): configured_remote_root(), find_new_temp_episodes(), list_dataset_roots(), EgoRecordingWorkerTest, FakeAdb

### Community 95 - "EncoderRuntime"
Cohesion: 0.12
Nodes (17): thread, unique_ptr, EncoderRuntime, channel, config, connected, driver, emittedMessageCount (+9 more)

### Community 96 - "summarize_episode_reports.py"
Cohesion: 0.23
Nodes (9): choose_primary_reason(), load_report(), main(), summarize_episode_reports(), summarize_sensor_reports(), summarize_simple_report(), summarize_video_reports(), report() (+1 more)

### Community 97 - "summarize_ugripper_rollout.py"
Cohesion: 0.22
Nodes (10): build_suite_summary(), choose_reason(), load_json(), main(), parse_suite_arg(), parse_version_arg(), suite_validation_reason(), summarize_rollout() (+2 more)

### Community 98 - "board_hmi_record_start_matrix.py"
Cohesion: 0.19
Nodes (8): HmiPort, render_color(), render_pulse(), rgb(), SensorSession, service_until(), standard(), xor()

### Community 100 - "CameraRecorder"
Cohesion: 0.07
Nodes (28): CameraRecorder(), ExitCode, FirstFramePtsUs, FirstFrameSystemTimeUs, HasFailure, HasStarted, HasWrittenOutput, IsRunning (+20 more)

### Community 101 - "scan_main_camera_mkv_issues.py"
Cohesion: 0.33
Nodes (6): GripperStereoCameraBlock, cameraModelEnum, distortionCoefficients, focalLength, principalPoint, reserved

### Community 102 - "usb_audio_utils.py"
Cohesion: 0.29
Nodes (14): alsa_hw(), alsa_plughw(), describe_usb_audio_device(), ensure_playback_mixer(), get_usb_audio_device(), _normalize_usb_name(), _parse_card_device_from_symlink(), Path (+6 more)

### Community 103 - "实施规则"
Cohesion: 0.07
Nodes (29): 1.0 图谱维护, 1.1 关键测试门禁, 1.2 可读性 / 可维护性必须显式改善, 1. 一次只做一个阶段目标, 2.1 C++20 默认约束, 2.2 logger 对齐约束, 2. 最小 `src/utils` 约束, 3.1 Naming / YAML 契约约束 (+21 more)

### Community 104 - "IntervalTree"
Cohesion: 0.14
Nodes (21): interval_vector, Scalar, pair, unique_ptr, Interval, start, stop, value (+13 more)

### Community 105 - "isConnected"
Cohesion: 0.40
Nodes (5): 7.1 默认产物, 7.2 metadata.json, 7.2 停录强校验, 7.3 停录强校验, 7. Episode 产物与检查

### Community 106 - "internal.hpp"
Cohesion: 0.17
Nodes (23): ByteArray, Compression, KeyValueMap, string, Status(), code, message, CompressionString() (+15 more)

### Community 107 - "hws"
Cohesion: 0.12
Nodes (19): hws script, chest_cam_enabled(), count_enabled(), count_online(), data_disk_fault(), entry_enabled(), is_data_disk_path(), is_exact_mountpoint() (+11 more)

### Community 108 - "parse_args"
Cohesion: 0.14
Nodes (12): error(), info(), level_enum, string, Initialize(), SetLevel(), SpdLogger::GetLevel(), SpdLogger::GetLevel_str() (+4 more)

### Community 109 - "gripper_hmi_calibration_data.h"
Cohesion: 0.15
Nodes (5): GripperSerialNumber, decodeSerialNumber(), encodeSerialNumber(), string, isPrintableSerialChar()

### Community 110 - "Config Entrypoints Baseline"
Cohesion: 0.14
Nodes (16): 1. 环境变量入口：`/etc/environment`, 2. 仓库内配置文件入口, 3. 持久化配置入口, 4.1 `config.txt`, 4.2 `calibration.txt`, 4.3 标定导入目录, 4. USB 导入侧配置入口, 5. 运行时控制文件入口 (+8 more)

### Community 111 - "TEST"
Cohesion: 0.14
Nodes (14): GripperHmiTransportResyncTest, SkipsBadChecksumAndResyncsToNextFrame, GripperHmiDriver::pauseIoThreadForExclusiveCommand(), GripperHmiDriver::readAndProcessAvailableLocked(), GripperHmiDriver::readBytesLocked(), GripperHmiDriver::writeFrameLocked(), handleIoFailureLocked, handleParsedFrame (+6 more)

### Community 112 - "Status"
Cohesion: 0.40
Nodes (5): 4.1 目录真源规则, 4.3 建议的 namespace 口径, 4.4 Shell / Python 命名规则, 4.5 YAML 规范规则, 4. 建议采用的目标规范

### Community 113 - "ReadJobQueue"
Cohesion: 0.12
Nodes (17): ByteOffset, ReadJob, DecompressChunkJob, chunkStartOffset, messageEndTime, messageIndexEndOffset, messageStartTime, vector (+9 more)

### Community 114 - "main_camera_xu_tool.cpp"
Cohesion: 0.40
Nodes (5): 4.1 App Shell 层, 4.2 Orchestration 层, 4.3 Domain Logic 层, 4.4 Adapter 层, 4. 重构目标不是“把测试补上”，而是“把代码改成可测试”

### Community 115 - "TEST"
Cohesion: 0.07
Nodes (29): ClassifiesMissingLeftAndRightCriticalDevices, ClassifiesUnknownStereoNotReadyWithoutSideDetails, HealthMonitorTest, IgnoresStereoStartupFaultsDuringGraceWindow, ReportsAllHmiDisconnected, ReportsAuxiliaryHmiPortInactivityAgainAfterRecovery, ReportsDiskFullBeforeDeviceChecks, ReportsDiskNotWritableBeforeDeviceChecks (+21 more)

### Community 116 - "PPMain And Ugripper Process/Thread Refactor Reference"
Cohesion: 0.40
Nodes (5): 9. 验收标准, 阶段 1 完成标准, 阶段 2 完成标准, 阶段 3 完成标准, 阶段 4 完成标准

### Community 117 - "main"
Cohesion: 0.13
Nodes (43): main(), parse_args(), Namespace, build_calibration_bin(), build_delivery_readme(), colorize(), column_letters_to_index(), create_delivery_bundle() (+35 more)

### Community 118 - "FileLogger"
Cohesion: 0.11
Nodes (24): ofstream, FileLogger, Enabled, EnsureParentDirExistsLocked, file_path_, Init, Log, mtx_ (+16 more)

### Community 119 - "isConnected"
Cohesion: 0.50
Nodes (5): diff_ranges_bytes(), final_header_refresh_size(), merge_ranges(), parse_mp4_boxes_from_prefix(), refresh_final_mp4_header()

### Community 120 - "`pp_main` 合并后 `ugripper` 正式测试方案"
Cohesion: 0.50
Nodes (4): 8.1 可以立即生效的规则, 8.2 需要按模块渐进迁移的规则, 8.3 不建议现在就做的事, 8. 命名迁移策略

### Community 121 - "fifo_utils.cc"
Cohesion: 0.21
Nodes (15): mode_t, BufferedFifoLineReader(), BufferedFifoLineReader::Open(), BufferedFifoLineReader::ReadAvailable(), string, vector, Close(), EnsureFifo() (+7 more)

### Community 122 - "readBytesLocked"
Cohesion: 0.50
Nodes (4): 9.3 用 `ctest` 批量跑, code:bash (cd /home/songwl/swl_ws/pp_main), code:bash (ctest --test-dir build/x86/test --output-on-failure -L camer), code:bash (ctest --test-dir build/x86/test --output-on-failure -R "Audi)

### Community 123 - "HmiController"
Cohesion: 0.12
Nodes (16): DebouncedButtonState, NowMsFn, HmiController(), down_button_, last_button_action_ms_, last_buttons_, now_ms_fn_, options_ (+8 more)

### Community 124 - "run_240_soak_safe.sh"
Cohesion: 0.19
Nodes (22): cleanup(), clear_maintenance_hold(), deploy_board_script(), episode_data_root(), maintenance_pull_cycle(), pull_and_optionally_clean_data(), pull_logs(), remote_test_alive() (+14 more)

### Community 125 - "可选 updater 包打包"
Cohesion: 0.09
Nodes (24): code:bash (PARALLEL=12 ./scripts/build_arm_deb_in_pp_arm_dev.sh), code:bash (./scripts/build_runtime_venv.sh), code:bash (./scripts/build_runtime_venv.sh --target-dir /tmp/ugripper-a), code:bash (./usb_updater_build.sh), code:bash (PKG_VERSION_SUFFIX=+merge1 ./usb_updater_build.sh), code:bash (build/package/updater/das-usb-updater_1.0.0_all.deb), code:bash (./usb_updater_transition_build.sh), code:bash (build/package/updater-transition/ugripper-usb-updater_1.2.5_) (+16 more)

### Community 126 - "9. 执行方式"
Cohesion: 0.12
Nodes (19): 9.1 先构建 standalone 目标, 9.2 构建 `pp_main/test`, 9.4 直接跑单个 `gtest` 二进制, 9.5 Host-only 组件 smoke, 9.6 板端二进制与服务 smoke, 9.7 板端压力测试, 9.8 第一版板端脚本入口, 9. 执行方式 (+11 more)

### Community 127 - "TEST"
Cohesion: 0.09
Nodes (20): AcceptsFastSecondShortPressAfterDefaultDebounce, ConfirmsPressAndReleaseAfterDefaultStableWindows, DebouncesRepeatedShortPresses, DualChordReleaseDoesNotEmitShortPress, EmitsLongPressInsteadOfShortPress, EmitsShortPressEventOnRelease, EmitsShutdownPromptAndShutdownForDualHold, FiltersPressPulseShorterThanDefaultStableWindow (+12 more)

### Community 128 - "usb_headset_volume_keys.py"
Cohesion: 0.50
Nodes (4): 6.1 audio / audio_en, 6.2 pack_script, 6.3 ASR / time_sync, 6. 特殊模块规则

### Community 129 - "Control Channel Ledger Baseline"
Cohesion: 0.15
Nodes (13): 1. 当前结论, 2. 通道台账, 3. 不纳入首批 ZMQ 迁移的实现细节, 4.1 `AudioCommandPort`, 4.2 `StereoSessionPort`, 4.3 `ShutdownRequestPort`, 4. 冻结后的接口边界建议, 5. 与 `pp_main` 的对齐方式 (+5 more)

### Community 132 - "UGripper 夹爪传感器掉线复现与日志采集 SOP"
Cohesion: 0.07
Nodes (43): 10. 拉回主机, 10. 结束采集, 11. 拉回主机, 11. 最少只记什么, 12. 最少只记什么, 1. 目标, 2. 用到的脚本, 3. 先准备 (+35 more)

### Community 133 - "PPMain And Ugripper Test-Oriented Refactor Reference"
Cohesion: 0.09
Nodes (22): ConfigManager, pp_main standalone mode, SingleProcessInstance, ugripper supervisor model, 11.1 可复用公共库, 11.2 仍带明显 standalone 语义的模块, 11. 并入 `pp_main` 时的建议落位, 12. 这份方案的验收标准 (+14 more)

### Community 134 - "test_camera_command_builder.cc"
Cohesion: 0.15
Nodes (13): BuildsMainCameraCommandWithExecEnvWrapper, BuildsHybridCommandWithStableFlags, BuildsHybridVideoFilterWithScaleAndFrameDrop, BuildsStereoSessionCommand, CameraCommandBuilderTest, MakeHybridCamera(), TEST(), BuildsHybridCommandWithStableFlags (+5 more)

### Community 137 - "parse_raw_pair_to_payload"
Cohesion: 0.13
Nodes (32): build_payload_template(), camera_model_enum_from_strings(), detect_raw_pair_from_dir(), fill_float_array(), fill_statistics(), finalize_payload(), GripperCalibrationDataV1, GripperCalibrationHeader (+24 more)

### Community 139 - "run_ugripper_calib_roundtrip_smoke.sh"
Cohesion: 0.19
Nodes (17): capture_imported_dirs(), DEVICE_SN, info(), LEFT_SN, on_exit(), require_cmd(), restore_payload(), resume_service_if_needed() (+9 more)

### Community 140 - "TestOptions"
Cohesion: 0.09
Nodes (23): vector, TestOptions, baudrate, beepDurationMs, beepDuty, beepFreq, color, customBeep (+15 more)

### Community 141 - "RecordingOrchestrator"
Cohesion: 0.11
Nodes (18): DiskValidationFailureMapsToError3Led, LedStateBridgeTest, MissingCameraBinaryMapsToError5Led, StartRecordingMapsReadyFlowToRecordingLed, StopRecordingSuccessReturnsToReadyLed, RuntimeLedState, string, vector (+10 more)

### Community 143 - "PPMain And Ugripper Process/Thread Refactor Reference"
Cohesion: 0.08
Nodes (25): 10.1 顶层, 10.2 worker, 10.3 公共层, 10. 推荐的目标架构, 13. 最终建议, 14. 关键源码参考, 16.1 `record_runtime` 迁移映射, 16.2 当前暂时不动的部分 (+17 more)

### Community 144 - "TEST"
Cohesion: 0.09
Nodes (22): AcceptsCalibrationAckTokenForCurrentAndNextPacket, ChecksRequiredChunkCoverageAndSequentialFallbackGate, ClassifiesRetryableAndAbortRecoveryStatuses, CollectCalibrationRangeFrameConsumesStatusAndDropsGarbage, CollectsCalibrationRangeChunkAndParsesHeaderCompletion, DecidesCalibrationChunkReadActions, DecidesCalibrationRangeRetryActions, DecidesCalibrationWriteChunkStatusActions (+14 more)

### Community 145 - "TEST"
Cohesion: 0.11
Nodes (22): AllowsExpectedTopicsWhenSpanGapWithinThreshold, AllowsLargeGapWhenThresholdNotConfigured, DetectsGapLargerThanConfiguredThreshold, DetectsMessageCountBelowConfiguredThreshold, DetectsSequenceGap, DetectsSpanGapBetweenExpectedTopics, DetectsSpanShorterThanConfiguredThreshold, DetectsTimestampRegression (+14 more)

### Community 146 - "gripper_hmi_driver_logic.h"
Cohesion: 0.08
Nodes (47): CalibrationChunkReadAction, CalibrationFrameScanResult, CalibrationRangeCollectResult, CalibrationReadRetryAction, CalibrationWriteChunkStatusAction, ExclusiveDrainAction, ExclusiveFrameScanResult, ReadBytesAction (+39 more)

### Community 147 - "test_camera_config.cc"
Cohesion: 0.13
Nodes (15): ugripper(), CameraSampleDir(), CameraConfigTest, LoadsLegacyFlatSample, LoadsSchemaV1Sample, path, RejectsLegacySampleWithoutOutputFiles, TEST() (+7 more)

### Community 148 - "FaysDataLogger"
Cohesion: 0.11
Nodes (14): ChannelId, AtrakIMU, acc, gyro, timestamp, unique_ptr, FaysDataLogger, camChannelId_ (+6 more)

### Community 149 - "TEST"
Cohesion: 0.09
Nodes (22): CheckRecorderProcessesUsesWorkerStatus, RecordingOrchestratorTest, RequiredEgoPullFailureMapsToError5, RequiredEgoStartFailureBlocksRecordingWithError5, StartRecordingLockFailureDoesNotLaunchWorkers, StartRecordingMissingCameraBinaryMapsToError, StartRecordingMissingSensorBinaryMapsToError, StartRecordingStereoStartFailureStopsWorkersAndSignalsError (+14 more)

### Community 150 - "TEST"
Cohesion: 0.14
Nodes (19): ConnectedEventMarksStateAsReconnectingAndPending, DisconnectedEventClearsPendingAndMarksDisconnected, GripperRefreshAction, GripperRefreshLogicTest, NoPendingRefreshProducesNoAction, PendingRefreshRequestsStateUntilGripperBecomesActive, PendingRefreshTransitionsToRefreshActionWhenReady, PendingRefreshWaitsForSideDevicesBeforeRequestingState (+11 more)

### Community 151 - "mcap_viewer.py"
Cohesion: 0.31
Nodes (10): as_vector(), format_binary_payload(), format_payload(), format_time_delta(), is_json_like_encoding(), Pick the most relevant encoding hint for the current message., Treat empty or 'json*' encodings (e.g. jsonschema) as JSON payloads., 智能格式化 Payload 数据     根据字段特征自动匹配显示格式：IMU / 编码器 / 通用JSON (+2 more)

### Community 153 - "build_runtime_venv.sh"
Cohesion: 0.10
Nodes (16): BUILD_RUNTIME_VENV_GENERATED_AT_UTC, BUILD_RUNTIME_VENV_GIT_COMMIT, BUILD_RUNTIME_VENV_HOST_MACHINE, BUILD_RUNTIME_VENV_PACKAGE_ARCH_HINT, BUILD_RUNTIME_VENV_PY_MM, BUILD_RUNTIME_VENV_PY_VER, BUILD_RUNTIME_VENV_PYPROJECT_SHA256, BUILD_RUNTIME_VENV_PYTHON_FILE_DESC (+8 more)

### Community 154 - "ValidateEpisodeProfileTest"
Cohesion: 0.22
Nodes (4): hardware_list(), metadata(), ValidateEpisodeProfileTest, video_details()

### Community 155 - "encoder_driver.cc"
Cohesion: 0.09
Nodes (30): EncoderData, disconnect(), EncoderDriver(), EncoderDriver::getEncoderPosition(), EncoderDriver::getEncoderVelocity(), EncoderDriver::getState(), EncoderDriver::readDataNonBlocking(), EncoderDriver::reset() (+22 more)

### Community 158 - "使用方式"
Cohesion: 0.09
Nodes (21): code:bash (sudo apt install pipx  # 系统python有保护，尝试用pipx安装uv), code:bash (uv add pytest==9.0.1), code:bash (uv add pytest>=9.0.1), code:bash (uv remove <dependency>), code:bash (uv remove pytest), code:bash (uv sync), code:bash (echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc # 可), code:bash (echo 'export UV_DEFAULT_INDEX="https://mirrors.aliyun.com/py) (+13 more)

### Community 159 - "27. Phase 1 头文件草案"
Cohesion: 0.10
Nodes (21): 27.10 `runtime_app.h`, 27.1 `runtime_types.h`, 27.2 `runtime_config.h`, 27.3 `subprocess_handle.h`, 27.4 `process_supervisor.h`, 27.5 `recording_orchestrator.h`, 27.6 `hmi_controller.h`, 27.7 `health_monitor.h` (+13 more)

### Community 161 - "import_camera_calibration.sh"
Cohesion: 0.14
Nodes (15): cleanup_stage_dir(), log(), prepare_gripper_payload_bin(), read_gripper_sn(), resolve_import_sides(), run_hmi_helper(), import_camera_calibration.sh script, SIDE_CAMCHAIN_FILES (+7 more)

### Community 166 - "MCAP CMake Builder"
Cohesion: 0.18
Nodes (10): Build and Install, Build MCAP Locally, code:bash ($ cd mcap_builder), code:cmake (find_package(mcap)), code:cmake (include(FetchContent)), Macros, MCAP CMake Builder, MCAP Is Installed (+2 more)

### Community 167 - "string"
Cohesion: 0.33
Nodes (6): CommandDiagStats, acks, recoveries, retries, sends, timeouts

### Community 168 - "`pp_main` 合并后 `ugripper` 正式测试方案"
Cohesion: 0.10
Nodes (19): 0.1 最近一次板端重跑结果, 0. 当前本地现状, 10. 推荐执行顺序, 11. 当前优先级建议, 12. 结论, 1. 范围与目标, 3.1 `L0`：Host-only Unit, 3.2 `L1`：Host-only Component Smoke (+11 more)

### Community 169 - "4.2 `SensorRecorder`"
Cohesion: 0.11
Nodes (26): 4.1 `CameraRecorder`, 4.2 `SensorRecorder`, 4.4 `UgripperRuntime`, 4.5 交叉模块与交付链, 4. 当前完整测试清单, 建议新增, 建议新增 `L0/L1`, 建议新增 `L0/L1` (+18 more)

### Community 170 - "build_deb.sh"
Cohesion: 0.22
Nodes (16): clean_target_install_root(), copy_first_existing(), copy_if_exists(), ensure_dir(), log_duration(), prepare_generated_dirs(), prepare_packaged_venv_source(), refresh_auto_release_manifest() (+8 more)

### Community 171 - "TEST"
Cohesion: 0.11
Nodes (21): ButtonActionRouterTest, ChordConsumesRemainingUpperButtonUntilBothRelease, ChordIsDisabledWhileRecording, CrossHandUpperChordFiresOnceAtTwoSeconds, EgoBindingUsesDistinctColorsAndToneDirections, ExtraButtonPreventsReservedCombinationFromMatching, FailuresAndRejectedMarkersUseErrorSemanticColors, HmiFeedbackPatternTest (+13 more)

### Community 174 - "CustomizedLogFormatter"
Cohesion: 0.50
Nodes (4): LoggingV3, SplitsCodedPayloadBeforeSubmittingToPpMainLogger, TEST(), ValidatesAndAppendsErrorCodePerPhysicalLine

### Community 176 - "VideoFrameQueue"
Cohesion: 0.11
Nodes (16): atomic, VideoFrame, bytes, channels, faysTsNs, height, publishTimeNs, width (+8 more)

### Community 177 - "HealthMonitorOptions"
Cohesion: 0.10
Nodes (21): string, vector, HealthMonitorOptions, critical_device_paths, disk_root, hmi_active_timeout_ms, poll_interval_ms, stereo_enabled (+13 more)

### Community 178 - "board_test_common.sh"
Cohesion: 0.14
Nodes (11): default_camera_config_yaml(), default_camera_recorder_bin(), default_check_gripper_ack_script(), default_check_hmi_event_script(), default_check_sensor_mcap_bin(), default_check_video_windows_script(), default_gripper_hmi_bin(), default_sensor_recorder_bin() (+3 more)

### Community 179 - "run_calibration.sh"
Cohesion: 0.23
Nodes (18): describe_zeroing_failure(), FAILED_SIDES, kill_tree(), log_error(), log_info(), notify_audio(), on_exit_cleanup(), resolve_audio_python() (+10 more)

### Community 183 - "TEST"
Cohesion: 0.12
Nodes (12): DetectsReadFrameLengthForPositionResponse, DetectsWriteAckFrameLength, RejectsUnsupportedReadPayloadLength, SensorProtocolTest, VerifiesModbusCrcBytes, TEST(), DetectsReadFrameLengthForPositionResponse, DetectsWriteAckFrameLength (+4 more)

### Community 184 - "AtrakGNSS"
Cohesion: 0.20
Nodes (10): AtrakGNSS, altitude, latitude, longitude, position_cov, position_cov_type, seq, service (+2 more)

### Community 185 - "EncoderData"
Cohesion: 0.11
Nodes (19): EncoderDriver::tryConsumeSample(), EncoderData, com, currentPosition, currentPositionRad, currentSpeed, currentSpeedRad, id (+11 more)

### Community 187 - "Naming And YAML Contract"
Cohesion: 0.11
Nodes (17): 1. 适用范围, 2.1 新增公共 namespace 目标, 2.2 文件与目录命名, 2.3 C++ 标识符命名, 2.4 include 路径规则, 2. 命名契约, 3. Camera YAML 现状事实, 4. Camera YAML v1 契约目标 (+9 more)

### Community 189 - "EnvUtilsTest"
Cohesion: 0.14
Nodes (13): IgnoresPartialPrefixMatches, ReadsAndTrimsQuotedValue, ReturnsEmptyWhenKeyMissing, string, ReadEnvValue(), TrimWhitespace(), path, string (+5 more)

### Community 193 - "Standalone Integration Inputs Baseline"
Cohesion: 0.22
Nodes (8): 1. 总体结论, 2. CameraRecorder, 3. SensorRecorder, 4. GripperHmiTool, 5. UgripperRuntime, 6. 暂不进入 standalone app 的内容, 7. B1 完成口径, Standalone Integration Inputs Baseline

### Community 197 - "需要固化的环境修改"
Cohesion: 0.12
Nodes (16): 1. 增加 `arm64` 架构, 2. 配置 `amd64` APT 源, 3. 新增 `arm64` ports 源, 4. 刷新 APT 索引, 5. 安装交叉编译与 `arm64` 目标依赖, code:bash (dpkg --add-architecture arm64), code:bash (cat >/etc/apt/sources.list <<'EOF'), code:bash (mkdir -p /etc/apt/sources.list.d) (+8 more)

### Community 198 - "Ugripper 重构后架构设计与当前实现"
Cohesion: 0.12
Nodes (16): 1.1 本仓中的实现真源, 1.2 本仓中的部署与口径真源, 1. 当前总体判断, 2. 重构目标现在应如何理解, 3. 当前总体结论, 4.1 `UgripperRuntime`, 4.2 `CameraRecorder`, 4.3 `SensorRecorder` (+8 more)

### Community 201 - "Repo Architecture Draft"
Cohesion: 0.10
Nodes (20): 10. 后续可选演进, 1. 文档目标, 2.0 真源目录与非真源目录, 2.1 产品主链, 2.2 部署与交付, 2.3 工具与现场辅助, 2.4 冻结区候选, 2.5 文档区 (+12 more)

### Community 202 - "Ugripper Refactor And Standalone Integration Plan"
Cohesion: 0.06
Nodes (32): Equivalence Checklist, Package Contents, Runtime Entrypoints, Runtime Paths, standalone integration inputs, Naming YAML Contract, 10. 最终判断, 1. 为什么要拆成两阶段 (+24 more)

### Community 203 - "5. 第一阶段：Ugripper Internal Refactor"
Cohesion: 0.09
Nodes (30): 5. 第一阶段：Ugripper Internal Refactor, A1. 基线冻结, A2. 建最小 `src/utils`, A3. 统一 C++ 日志, A4. 收低风险 helper, A5. 建立 camera 抽象层, A6. 清理硬编码路径, A7. 拆高风险脚本 (+22 more)

### Community 204 - "Deploy / Script Boundary Baseline"
Cohesion: 0.07
Nodes (33): Shutdown Request File /tmp/umi_shutdown_request, 1. 归属总表, 2.1 `UgripperRuntime`, 2.2 `GripperHmiTool`, 2.3 `SensorRecorder / zeroing`, 2. 与 app 的真实边界, 3.1 Update 链路, 3.2 Calibration 链路 (+25 more)

### Community 205 - "WriteCameraRecorderFaultJson"
Cohesion: 0.50
Nodes (4): TimevalToUs(), V4l2BufferSystemTimeUs(), timeval, v4l2_buffer

### Community 207 - "UMI Calibration Read/Write Protocol"
Cohesion: 0.13
Nodes (14): Agent Overview, ppmain-ugripper-test-plan.md, pp_main merged test summary, REFACTOR_LOG, Refactor Log, YYYY-MM-DD - <short-title>, 使用规则, 条目模板 (+6 more)

### Community 208 - "readBytesLocked"
Cohesion: 0.27
Nodes (11): ExclusiveFrameReadResult, vector, GripperHmiDriver::readCalibrationFrameLocked(), GripperHmiDriver::readDataFrameLocked(), GripperHmiDriver::readDataOrStatusFrameLocked(), GripperHmiDriver::readExpectedStatusFrameLocked(), GripperHmiDriver::readRawDataFrameLocked(), GripperHmiDriver::readRawDataOrStatusFrameLocked() (+3 more)

### Community 210 - "HealthMonitor"
Cohesion: 0.12
Nodes (16): HealthStatus, PollResult, Dependencies, NowMsFn, HealthMonitor, dependencies_, EvaluateHealth, now_ms_fn_ (+8 more)

### Community 213 - "TEST_F"
Cohesion: 0.17
Nodes (11): ReturnsFalseForEmptyFile, ReturnsFalseForMissingFile, ReturnsTrueForNonEmptyFile, FileExistsAndNotEmpty(), string, path, string, testing::Test (+3 more)

### Community 214 - "Options"
Cohesion: 0.12
Nodes (16): path, string, Options, allow_missing, codec, config_yaml, config_yaml_explicit, control_file (+8 more)

### Community 215 - "GripperCalibrationDataV1"
Cohesion: 0.20
Nodes (10): GripperCalibrationDataV1, extrinsics, header, imu0, reserved, residuals, rgbCamera, stereoCam0 (+2 more)

### Community 216 - "gripperStateIndexForSide"
Cohesion: 0.09
Nodes (29): clearGripperRuntimeStateForSide, markTactileReferencePendingForSide, noteRestoreUsbManualInsert, sideHasAnyRestoreUsbDevice, areSideCriticalDevicesReady(), boolText(), clearGripperRuntimeStateForSide(), gripperStateIndexForSide() (+21 more)

### Community 217 - "SubprocessHandle"
Cohesion: 0.10
Nodes (21): pid_t, string, SubprocessHandle, IsRunning, last_exit_code_, LastExitCode, name_, Pid (+13 more)

### Community 219 - "Ugripper V2 Overview"
Cohesion: 0.20
Nodes (9): 11. 当前使用注意点, 1. 当前系统一句话, 2. 安装布局与入口, 3. 模块总览, 4. 启动链路, 5.1 空闲态与阈值, 5.2 按键动作, 5. 状态机与按键行为 (+1 more)

### Community 220 - "8. 灯效、音频与关键路径"
Cohesion: 0.13
Nodes (15): 8.1 当前状态灯语义, 8.2 关键持久化与临时路径, 8.3 PERF 日志, 8.3 软件录制控制, 8.3 音频链路关键行为, 8.4 PERF 日志, 8.4 相关辅助单元, 8.4 音频链路关键行为 (+7 more)

### Community 225 - "silenceBeep"
Cohesion: 0.10
Nodes (29): getSnapshot(), setBeepEnabled(), setBeepState(), setLedColor(), setLedEffect(), silenceBeep(), isConnected(), connect() (+21 more)

### Community 228 - "postinst"
Cohesion: 0.24
Nodes (12): postinst script, apply_service_user_override(), calibration_file_is_valid(), disable_continuous_ntp_services(), ensure_exfat_autoload(), ensure_persist_calibration(), force_cleanup_recording_processes(), resolve_dynamic_service_user() (+4 more)

### Community 230 - "UgripperRuntime/audio/usb_audio_utils.py"
Cohesion: 0.24
Nodes (11): describe_usb_audio_device(), get_usb_audio_device(), _normalize_usb_name(), _parse_card_device_from_symlink(), Path, RuntimeError, _read_capture_node(), require_usb_audio_device() (+3 more)

### Community 231 - "HmiHealthSnapshot"
Cohesion: 0.13
Nodes (15): string, vector, HmiHealthSnapshot, disconnected_ports, has_connected_device, inactive_port_details, inactive_ports, input_active (+7 more)

### Community 233 - "6. 第二阶段：Standalone Integration"
Cohesion: 0.09
Nodes (25): 6.0.1 第二阶段统一门禁, 6.0 进入第二阶段的前提, 6. 第二阶段：Standalone Integration, B1. 并仓准备, B2.1 `CameraRecorder`, B2.2 `SensorRecorder`, B2.3 `GripperHmiTool`, B2.4 `UgripperRuntime` (+17 more)

### Community 234 - "MainCameraPtsResult"
Cohesion: 0.14
Nodes (12): CameraTimingTest, MinimallyClampsDuplicateTimestamp, MinimallyClampsTimestampRollback, PreservesRealCaptureGap, MainCameraPtsResult, adjustment_us, clamped, pts_us (+4 more)

### Community 235 - "test-ugripper-rollout"
Cohesion: 0.14
Nodes (13): 1. 先压缩，再展开, 2. 默认先核对现状，不重复劳动, 3. 文档更新规则, 4. 测试推进优先级规则, 5. 板端验证边界, 6. 最小验证规则, test-ugripper-rollout, 固定执行顺序 (+5 more)

### Community 236 - "todo-hub"
Cohesion: 0.14
Nodes (13): 1. 建立或读取 TODO, 2. 规划候选任务, 3. 启动指定任务, 4. 查看进度, 5. 向子会话下达指令, references/, scripts/, todo-hub (+5 more)

### Community 238 - "main"
Cohesion: 0.21
Nodes (11): EpochValuesArePositiveAndConsistent, Open, CurrentEpochMs(), CurrentEpochUs(), CurrentSteadyMs(), string, main(), writeAll() (+3 more)

### Community 239 - "error_code.h"
Cohesion: 0.25
Nodes (7): ErrorCodeMapping, MapsUniqueLegacyTypesWithoutPayloadInspection, RequiresDirectContextForAmbiguousLegacyTypes, string_view, UgrCodeForErrorType(), TEST(), UnknownContextUsesOnlyTheDocumentedFallback

### Community 240 - "HmiFeedbackPattern"
Cohesion: 0.14
Nodes (14): HmiFeedbackSide, HmiFeedbackEvent, FeedbackPatternFor(), array, HmiFeedbackPattern, beep_frequencies_hz, beep_side, blue (+6 more)

### Community 242 - "TactileFrameMetrics"
Cohesion: 0.14
Nodes (14): TactileFrameMetrics, damaged, droppedArtifactComponentCount, droppedArtifactComponentPixels, gain, maxResidualComponentPixels, offset, rawResidualArea (+6 more)

### Community 243 - "LoggingV3Test"
Cohesion: 0.22
Nodes (3): load_module(), LoggingV3Test, Path

### Community 244 - "safe_ntp_sync.sh"
Cohesion: 0.40
Nodes (13): check_network(), cleanup(), disable_ntp_if_needed(), log(), log_error(), recording_lock_active(), safe_ntp_sync.sh script, stop_continuous_ntp_services() (+5 more)

### Community 245 - "7.4 timing 模型约定"
Cohesion: 0.18
Nodes (12): 7.1 去掉对 camera name 的硬编码编排, 7.2 把 runtime 对相机的理解从“字符串常量”改为“查询结果”, 7.3 timing merge 仍然可以保留在 runtime，但输入应标准化, 7.4 timing 模型约定, 7. `record_runtime` 应怎么改, code:json ({), code:cpp (enum class TimingSource {), merge 时的 contract (+4 more)

### Community 246 - "2. 重构原则"
Cohesion: 0.15
Nodes (13): 2.0 C++ 标准默认约束, 2.1 先收边界，再动结构, 2.2 先行为等价，再做整理, 2.3.1 通信通道先做 ledger，再做接口, 2.3 先最小 utils，再重型基础设施, 2.4 Shell 先做“编排化”, 2.5 PR 只做一类事情, 2.6.1 日志能力先对齐 API，再对齐后端 (+5 more)

### Community 247 - "Phase 7：脚本与 Python 收口"
Cohesion: 0.13
Nodes (23): Phase 0：架构建模, Phase 7.1 `run_record.sh`, Phase 7.2 `auto_update/usb_auto_update.sh`, Phase 7.2 `usb_auto_update.sh`, Phase 7.3 `auto_calibration/run_calibration.sh`, Phase 7.3 `run_calibration.sh`, Phase 7.4 `audio/*.py`, Phase 7：脚本与 Python 收口 (+15 more)

### Community 248 - "10. 常用检查与排障入口"
Cohesion: 0.15
Nodes (13): 10.1 服务管理, 10.2 录制相关进程, 10.3 U 盘流程日志, 10.4 运行日志, 10.5 设备映射优先检查, 10.6 快速定位建议, 10.7 tactile serial 定向核对, 10.8 仓库内测试脚本 (+5 more)

### Community 251 - "read_text"
Cohesion: 0.83
Nodes (3): load_lines(), main(), parse_diag_fields()

### Community 252 - "check_sensor_mcap.cc"
Cohesion: 0.43
Nodes (7): Options, string, vector, JoinFailures(), main(), ParseArgs(), PrintUsage()

### Community 258 - "MotionAlertMessage"
Cohesion: 0.15
Nodes (11): MotionAlertMessage, accelExcess, active, gyroMagnitude, magic, reason, reserved0, reserved1 (+3 more)

### Community 259 - "AtrakIntrinsics"
Cohesion: 0.15
Nodes (13): AtrakIntrinsics, cam_model, cx, cy, distortion, distortion_model, fx, fy (+5 more)

### Community 260 - "AtrakObject2D"
Cohesion: 0.15
Nodes (13): AtrakObject2D, angle, bbox, cls, confidence, extra_info_mask, id, pose (+5 more)

### Community 262 - "GripperHmiSnapshot"
Cohesion: 0.10
Nodes (20): GripperHmiDriver::getSnapshot(), GripperHmiDriver::setBeepState(), GripperHmiDriver::waitForKeyChange(), GripperHmiProtocol::buildUnknownKeyReport(), GripperHmiSnapshot, active, beepState, beepStateCount (+12 more)

### Community 263 - "main"
Cohesion: 0.12
Nodes (18): string, EncoderDriver::EncoderDriver(), EncoderDriver::markDisconnected(), getState(), resetBaudrate(), connect, disconnect, getState (+10 more)

### Community 265 - "Test Layout"
Cohesion: 0.15
Nodes (12): code:bash (bash test/scripts/camera_test.sh), code:bash (BASE_DIR=/tmp/ugripper_cam_check DURATION=30 CAMERA_SET=non_), code:bash (python3 test/scripts/scan_main_camera_mkv_issues.py /mnt/dat), code:bash (sudo bash /dev/shm/ugripper_hmi_test/board_gripper_hmi_link_), Field Scripts, GoogleTest, Test Layout, test/scripts/* (+4 more)

### Community 267 - "5. 推荐的类设计"
Cohesion: 0.17
Nodes (12): 5.1 公共类型层, 5.2.1 `CameraNode / CameraDevice / CameraSession` 关系图, 5.2 相机规范类, 5.3 recorder backend 层, 5.4 Stereo 专属组合类, 5.5 相机注册表, 5. 推荐的类设计, code:cpp (#pragma once) (+4 more)

### Community 268 - "8.3 Phase 1 最小头文件草案"
Cohesion: 0.22
Nodes (10): 8.1 在 `ugripper` 内部的建议切分, 8.2 Phase 1 施工目录树, 8.3 Phase 1 最小头文件草案, 8.4 合并到 `pp_main` 时的建议落点, 8. 推荐的目录和代码切分方式, code:text (src/camera_domain/), code:cpp (#pragma once), code:text (Idle -> Starting -> Running) (+2 more)

### Community 269 - "Package Contents Baseline"
Cohesion: 0.17
Nodes (11): 10. 当前包安装后自动动作, 1. 包元信息, 2. 当前组包方式, 3. 当前默认会进入包的 app/install 内容, 4. 当前默认会进入包的仓库真源脚本/资源, 5. 当前不会进入包的 legacy 兼容内容, 6. 当前安装的 systemd 单元, 7. 当前安装的 udev 规则 (+3 more)

### Community 270 - "Runtime Paths Baseline"
Cohesion: 0.17
Nodes (11): 0. 仓库内路径真源分类, 1. 二进制路径, 2. 脚本路径, 3. 配置与资源路径, 4. 环境与持久化路径, 5. 运行时临时路径, 6. 数据盘与日志路径, 7. 设备节点路径 (+3 more)

### Community 274 - "fays_tail_imu_check.py"
Cohesion: 0.60
Nodes (5): classify_topic(), fail(), load_chunk_records(), main(), validate_tail_imu()

### Community 275 - "setup_arm_build_env.sh"
Cohesion: 0.38
Nodes (11): apt_arm(), cat_summary(), download_arm_packages(), extract_sysroot(), install_host_tools(), prepare_private_apt_root(), require_tool(), setup_arm_build_env.sh script (+3 more)

### Community 276 - "BufferedFifoLineReader"
Cohesion: 0.17
Nodes (11): BufferedFifoLineReader, ClearBuffer, Close, fd, IsOpen, max_buffer_bytes_, path_, pending_ (+3 more)

### Community 277 - "currentSteadyMs"
Cohesion: 0.19
Nodes (13): GripperHmiDriver::disconnect(), GripperHmiDriver::handleIoFailureLocked(), GripperHmiDriver::handleParsedFrame(), GripperHmiDriver::ioLoop(), GripperHmiDriver::isActive(), GripperHmiDriver::logConnectFailureLocked(), GripperHmiDriver::resetConnectFailureLogLocked(), logIoSummaryLocked (+5 more)

### Community 278 - "RecordingOrchestratorOptions"
Cohesion: 0.17
Nodes (12): string, RecordingOrchestratorOptions, camera_codec, camera_recorder_bin, sensor_recorder_bin, session_camera_streams_csv, stereo_enabled, stereo_finalize_timeout_ms (+4 more)

### Community 279 - "camera_test.sh"
Cohesion: 0.36
Nodes (9): log_note(), run_cam(), run_cam_pair_merge(), run_non_main(), run_non_main_merge_tactile(), run_sequence(), camera_test.sh script, start_monitors() (+1 more)

### Community 280 - "PPMain And Ugripper Naming/YAML Refactor Reference"
Cohesion: 0.20
Nodes (9): 10. 验收标准, 11. 最终建议, 1. 先讲结论, 7. 推荐的模块与配置映射, 9. 作为完整重构计划输入时，建议优先落地的 12 项任务, PPMain And Ugripper Naming/YAML Refactor Reference, pp_main naming baseline, runtime protocol JSON (+1 more)

### Community 281 - "4. 建议采用的目标规范"
Cohesion: 0.33
Nodes (6): 4.2 C++ 命名规则, code:cpp (#include "camera_recorder/camera_config.h"), code:cpp (#include "gripper_hmi_driver.h"), 内部实现, 目录和文件, 类型与接口

### Community 282 - "4.3 `Gripper` / `HMI`"
Cohesion: 0.18
Nodes (11): 4.3 `Gripper` / `HMI`, 建议新增 `L0/L1`, 建议新增 `L0/L1`, 建议新增 `L2/L3`, 建议新增 `L2/L3`, 建议新增 `L4`, 建议新增 `L4`, 当前已有 (+3 more)

### Community 284 - "Reference-Aligned Refactor Next Steps After A3"
Cohesion: 0.18
Nodes (10): 1. 当前基线, 3. 对当前主计划的调整结论, 5.1 必须更新, 5.2 按需更新, 5.3 每步记录最少应包含, 5.4 失败门禁规则, 5. 每一步的记录要求, 6. 建议的 PR 粒度 (+2 more)

### Community 287 - "Ugripper Debug Playbook"
Cohesion: 0.18
Nodes (10): 1. 服务启动失败, 2. 录制启动/停止异常, 3. 网络 / 环境异常, 4. 相机录制异常, 5. 传感器异常, 6. Episode 校验失败, 7. USB 升级/标定导入失败, 8. 打包/恢复异常 (+2 more)

### Community 288 - ".FaysRecorder"
Cohesion: 0.50
Nodes (3): PrintCalibrationInfo(), PrintDeviceInfo(), printTransform()

### Community 292 - "1.2 Block 内字段映射"
Cohesion: 0.18
Nodes (11): 1.1 整体块布局, 1.2 Block 内字段映射, 1.3 当前 payload 已存与未存, 1. Bin Byte Mapping, `extrinsics` @ `0x0B0`, `header` @ `0x000`, `imu0` @ `0x150`, `residuals` @ `0x170` (+3 more)

### Community 295 - "主摄标定数据结构说明"
Cohesion: 0.08
Nodes (25): 1. 背景, 2. 当前 payload 总览, 3. C/C++ 结构定义, 4. 结构大小, 5.1 MainCameraCalibrationHeader, 5.2 MainCameraIntrinsicsBlock, 5.3 尾部 `reserved[944]`, 5. 字段说明 (+17 more)

### Community 296 - "run_record.sh"
Cohesion: 0.29
Nodes (10): cleanup(), cleanup_old_logs(), is_storage_ready(), log_error(), log_info(), mpp_debug, mpp_log_level, mpp_syslog_perror (+2 more)

### Community 297 - "run_fays_record.sh"
Cohesion: 0.42
Nodes (9): append_ld_library_path(), ensure_fifo(), ensure_fixed_fays_symlinks(), fays_error(), fays_log(), require_config_file(), send_control_cmd(), run_fays_record.sh script (+1 more)

### Community 298 - "FaysMcapSummary"
Cohesion: 0.18
Nodes (11): FaysMcapSummary, cameraMessageCount, cameraPublishSpanNs, cameraSpanNs, firstCameraLogTimeNs, firstCameraPublishTimeNs, imuMessageCount, lastCameraLogTimeNs (+3 more)

### Community 299 - "board_ch9344_interference_matrix.sh"
Cohesion: 0.33
Nodes (7): analyze_phase(), collect_snapshot(), restore_service(), run_phase(), board_ch9344_interference_matrix.sh script, stop_sensor(), usage()

### Community 301 - "5. 推荐的 YAML 重构方式"
Cohesion: 0.22
Nodes (9): 5.1 camera 配置的目标结构, 5.2 为什么要这样分层, 5.3 `record_runtime` 不应继续只有 C++ 默认值, 5.4 `sensor_recorder` 也应有自己的配置入口, 5.5 JSON 临时协议应和正式 schema 分开, 5. 推荐的 YAML 重构方式, code:yaml (cameras:), code:yaml (CameraRecorder:) (+1 more)

### Community 302 - "21.1 配置类别"
Cohesion: 0.20
Nodes (10): 21.1 配置类别, 21.2 变更时机策略, 21.3 与 `ConfigManager` 的对齐策略, 21. 配置收口方案, feature flag 类, timeout / polling 类, 设备类, 调试/诊断类 (+2 more)

### Community 303 - "24. 并入 `pp_main` 前的规范对齐清单"
Cohesion: 0.20
Nodes (10): 24.1 命名和目录, 24.2 logger, 24.3 配置, 24.4 `utils` 依赖方向, 24.5 shell 依赖, 24.6 standalone target 约定, 24.7 头文件和实现边界, 24.8 singleton / static (+2 more)

### Community 304 - "25. 函数级迁移表"
Cohesion: 0.20
Nodes (10): 25.1 顶层生命周期与主循环, 25.2 录制编排, 25.3 按钮与 HMI, 25.4 音频, 25.5 stereo 会话, 25.6 健康检查, 25.7 进程监督, 25.8 Episode 与文件治理 (+2 more)

### Community 305 - "6.1 目标职责切分"
Cohesion: 0.20
Nodes (10): 6.1 目标职责切分, 6.2 推荐依赖方向, 6.3 拆分顺序建议, 6. `record_runtime` 应该怎么拆, `HealthMonitor`, `HmiController`, `ProcessSupervisor`, `RecordingOrchestrator` (+2 more)

### Community 306 - "2. 合并后的总原则"
Cohesion: 0.20
Nodes (10): 2.1 不推翻 `A1-A3`, 2.2 测试体系要前置，但不一次性铺满, 2.3 不先做大规模 rename, 2.4 保留 supervisor 多进程模型, 2.5.1 logger / 通信 / 调度的靠拢方式, 2.5 camera 重构优先拆“域模型”，不先碰深层设备访问, 2.6 YAML 先兼容，后升级, 2.7 每一步只做一类事情 (+2 more)

### Community 307 - "4. A3 之后的保守执行步骤"
Cohesion: 0.20
Nodes (10): 4. A3 之后的保守执行步骤, Step 1：Reference Freeze And Traceability Alignment, Step 2：正式测试入口最小落地, Step 3：路径台账与真源目录冻结, Step 4：Naming/YAML Contract Freeze, Step 5：Sensor Common 提升为可测试模块, Step 6：Camera Pure Logic First, Step 7：Runtime Process Boundary Refactor (+2 more)

### Community 308 - "8. Step 9 之后、Stage B 之前的新增收口项"
Cohesion: 0.20
Nodes (10): 8.1 目标与边界, 8.2 `camera_recorder` 的最终目标形态, 8.3 `record_runtime` 的最终目标形态, 8.4 logger 与 `pp_main` 的差距判断, 8.5 任务调度与线程边界的收口路线, 8.6 通信向 ZMQ 靠拢的渐进路线, 8.7.1 当前进度判断, 8.7 建议执行顺序 (+2 more)

### Community 309 - "3.3 当前主要问题"
Cohesion: 0.20
Nodes (10): 3.1 当前主链入口, 3.2 当前主链模块, 3.3 当前主要问题, 3. 当前代码现状, camera 处理职责混杂, helper 散落且重复, 日志体系不统一, 硬编码路径较多 (+2 more)

### Community 310 - "Phase 4：路径台账与路径封装"
Cohesion: 0.12
Nodes (17): Phase 1：基线冻结, Phase 4.1 路径台账, Phase 4.2 路径封装, Phase 4：路径台账与路径封装, 主仓库 / ARM 阶段验证, 子步骤, 完成标准, 完成标准 (+9 more)

### Community 311 - "mount_data_disk.sh"
Cohesion: 0.36
Nodes (7): cleanup_mountpoint(), ensure_mountpoint_dir(), mount_active(), run_mount_command(), run_unmount_with_timeout(), mount_data_disk.sh script, source_exists()

### Community 312 - "board_encoder_cmd_ack_latency.py"
Cohesion: 0.52
Nodes (6): configure_serial(), crc16_modbus(), main(), percentile(), Path, run_side()

### Community 313 - "add-feature"
Cohesion: 0.20
Nodes (9): add-feature, 代码修改规则, 发布与编译联动规则（新增）, 固定执行顺序, 文档同步规则（docs）, 版本号决策与修改规则（新增）, 计划反馈阶段处理（强约束）, 输出格式要求 (+1 more)

### Community 325 - "ProcessSpec"
Cohesion: 0.20
Nodes (10): ProcessRestartPolicy, ProcessStopMode, vector, ProcessSpec, argv, inherited_fds, name, restart_policy (+2 more)

### Community 326 - "ProcessStatus"
Cohesion: 0.20
Nodes (10): ProcessState, string, ProcessStatus, expected_exit, last_exit_code, last_stop_reason, pid, ready (+2 more)

### Community 328 - "log_fmtter.cc"
Cohesion: 0.21
Nodes (12): log_msg, memory_buf_t, level_enum, string_view, CustomizedLogFormatter::extract_class_name(), CustomizedLogFormatter::extract_function_name(), CustomizedLogFormatter::format(), CustomizedLogFormatter::get_level_color_start() (+4 more)

### Community 330 - "GripperResidualsBlock"
Cohesion: 0.20
Nodes (10): GripperResidualsBlock, accelerometerErrorImu0MS2, gyroscopeErrorImu0RadS, reprojectionErrorCam0Px, reprojectionErrorCam1Px, GripperStatisticsBlock, mean, median (+2 more)

### Community 332 - "crc32.hpp"
Cohesion: 0.24
Nodes (6): CRC32Table(), table, crc32Update(), getUint32LE(), array, byte

### Community 333 - "button_action_router.cc"
Cohesion: 0.13
Nodes (9): ButtonActionRouter::ButtonActionRouter(), optional, vector, SnapshotMask(), TaskMarkerTracker::Mark(), TaskMarkerTracker::task_start_s(), TaskMarkerTracker::task_stop_s(), Update() (+1 more)

### Community 334 - "ButtonChordDefinition"
Cohesion: 0.12
Nodes (19): ChordState, ButtonActionRouter::Update(), ButtonActionRouter(), definitions_, release_latch_mask_, Reset, SnapshotMask, states_ (+11 more)

### Community 336 - "prepare_240_repro.sh"
Cohesion: 0.38
Nodes (7): cleanup_auto_record(), require_option_value(), scp_cmd(), prepare_240_repro.sh script, ssh_cmd(), start_ssh_master(), usage()

### Community 337 - "18. `ProcessSupervisor` 合同"
Cohesion: 0.22
Nodes (9): 18.1 管理对象, 18.2 建议状态, 18.3 `start()` 语义, 18.4 `stop()` 语义, 18.5 expected exit 与 unexpected exit, 18.6 restart policy, 18.7 stdout / stderr, 18.8 liveness / readiness (+1 more)

### Community 338 - "2. `pp_main` 的进程/线程管理模式"
Cohesion: 0.22
Nodes (9): 2.1 顶层模式：薄 `main()` + 业务总装对象, 2.2 单实例保护：`SingleProcessInstance`, 2.3 统一周期调度：`TaskScheduler`, 2.4 运行时配置中心：`ConfigManager`, 2.5 通信对象自己持有后台线程, 2.6 业务总装对象负责资源组装和 Stop 顺序, 2.7 `pp_main` 现有设计的优点, 2.8 `pp_main` 不是完美范式，复用时要保留判断 (+1 more)

### Community 339 - "3. `ugripper` 当前的进程/线程管理模式"
Cohesion: 0.22
Nodes (9): 3.1 顶层实际链路：`systemd -> shell -> runtime -> workers`, 3.2 `record_runtime` 的真实角色, 3.3 `ProcessRunner`：当前已存在 supervisor 雏形, 3.4 `camera_recorder` 是独立重链路进程, 3.5 `sensor_recorder` 是阻塞 I/O 型多线程进程, 3.6 `GripperHmiDriver` 已经是独立 I/O 线程模型, 3.7 `ugripper` 当前模式的优点, 3.8 当前模式的主要问题 (+1 more)

### Community 340 - "check_gripper_ack_log.py"
Cohesion: 0.70
Nodes (4): load_lines(), main(), parse_count_requirement(), parse_diag_fields()

### Community 341 - "bump_release_versions.sh"
Cohesion: 0.31
Nodes (5): bump_release_versions.sh script, update_ugripper_versions(), update_version_file(), update_version_file_to(), usage()

### Community 342 - "debug-ugripper"
Cohesion: 0.22
Nodes (8): debug-ugripper, 只读快速验证规则, 固定执行顺序, 必须等待确认的动作, 资源使用, 输出要求, 项目专用排查入口, 默认问题收敛方式

### Community 343 - "git-commit"
Cohesion: 0.22
Nodes (8): git-commit, 二次确认触发条件, 伴随改动归属规则, 常用 type 选择, 执行顺序, 提交信息标准, 提交范围规则, 输出要求

### Community 344 - "test-new-feature"
Cohesion: 0.22
Nodes (8): test-new-feature, 代码修改规则, 固定执行顺序, 局部测试与验证规则（新增）, 文档同步规则（docs）, 计划反馈阶段处理（强约束）, 输出格式要求, 验收与检查默认策略

### Community 345 - "Runtime Protocol"
Cohesion: 0.22
Nodes (8): 1. 机器可读状态, 2. 人类可读状态, Runtime Protocol, 会话续接策略, 启动持久化策略, 子 agent 进度上报, 子会话工作目录, 指令转发协议

### Community 354 - "log_string.h"
Cohesion: 0.14
Nodes (15): Exception, format_string, ostringstream, source_location, AppendUgrErrorCodeToLines(), AppendUgrErrorCodeToPhysicalLines(), string, string_view (+7 more)

### Community 355 - "TEST"
Cohesion: 0.22
Nodes (8): OverwritesExistingRequestFile, ShutdownRequestPortTest, path, TempDir, path_, TEST(), WaitsForSystemActionResult, WritesShutdownRequestFile

### Community 356 - "ugripper_log_writer.py"
Cohesion: 0.39
Nodes (8): main(), _new_outer(), normalize_existing(), normalize_line(), parse_args(), Namespace, Path, threading_native_id()

### Community 358 - "board_encoder_to_hmi_interference_matrix.sh"
Cohesion: 0.44
Nodes (7): analyze_hmi_log(), collect_snapshot(), restore_service(), run_phase(), board_encoder_to_hmi_interference_matrix.sh script, stop_monitor(), usage()

### Community 359 - "PPMain And Ugripper Camera Refactor Reference"
Cohesion: 0.10
Nodes (20): 11.1 `CameraRegistry` 是 runtime 和 camera_recorder 共用的真源, 11.2 `--only` 这类 CLI 覆写只允许做筛选，不允许改 spec 语义, 11.3 deployment 层的设备路径变更不建议由 CLI 临时覆盖, 11. runtime / recorder / registry 边界补充, 12. 最终建议, 1. 先讲结论, 2.1 类型先统一，再谈实现, 2.2 生命周期接口清晰 (+12 more)

### Community 360 - "3. `ugripper` 当前在命名和 YAML 上的核心问题"
Cohesion: 0.25
Nodes (8): 3.1 目录真源不唯一, 3.2 C++ 命名风格混用, 3.3 include 路径风格不统一, 3.4 文件和脚本命名仍有历史噪音, 3.5 YAML 结构过平，语义过载, 3.6 YAML 和运行时语义没有形成单一真源, 3.7 YAML 字段设计和实现使用存在偏差, 3. `ugripper` 当前在命名和 YAML 上的核心问题

### Community 361 - "6. 建议的重构顺序"
Cohesion: 0.25
Nodes (8): 6. 建议的重构顺序, code:text (test/), Phase 0：冻结真源, Phase 1：先定规则，不做全仓 rename, Phase 2：抽出配置库, Phase 3：配置驱动替代硬编码, Phase 4：测试补齐, Phase 5：并仓前收口

### Community 362 - "15.2 Phase 1 结束后的目标形态"
Cohesion: 0.25
Nodes (8): 15.1 为什么 Phase 1 不直接并入 `pp_main`, 15.2.1 入口, 15.2.2 进程, 15.2.3 线程, 15.2.4 配置, 15.2.5 测试, 15.2 Phase 1 结束后的目标形态, 15. 设计补丁：把本文档从“方向文档”补到“落地设计输入”

### Community 363 - "2.1 当前已有测试模块"
Cohesion: 0.25
Nodes (8): 2.1 当前已有测试模块, 2.2 当前实际覆盖边界, 2.3 当前与更新后代码未完全对齐的测试空白, 2. 当前 `pp_main` 已有测试设计, `camera_recorder`, `gripper_hmi`, `record_runtime`, `sensor_recorder`

### Community 364 - "7. 分阶段落地路线"
Cohesion: 0.25
Nodes (8): 7.1 Phase 0：判定标准与验收阈值冻结, 7.2 Phase 1：运行时观测点补齐, 7.3 Phase 2：第一版离线分析器落地, 7.4 Phase 3：规则下沉到 `host-only`, 7.5 Phase 4：板端标准测试脚本化, 7.6 Phase 5：发版前正式回归与报告化, 7. 分阶段落地路线, 第一轮执行阈值

### Community 365 - "10. 推荐执行顺序"
Cohesion: 0.25
Nodes (8): 10.1 Phase 与测试目标对齐表, 10. 推荐执行顺序, Phase A0：采用当前工作树为基线, Phase A1：先让现有库目标进入正式测试, Phase A2：把 `sensor_recorder` 的公共逻辑从“复用源文件”提升为“可测试模块”, Phase A3：拆 `camera_recorder` 的纯逻辑, Phase A4：拆 `record_runtime` 的状态机和 episode 逻辑, Phase A5：补齐 `camera` / `runtime` 组件测试，并收敛并仓映射

### Community 366 - "Ugripper Refactor Plan"
Cohesion: 0.12
Nodes (16): 1. 目标, 3. 当前主链范围, 4. 阶段计划, 5. 推荐执行顺序, 6. 推荐 PR 顺序, 8. 完成标志, Phase 3：统一 C++ 日志, Ugripper Refactor Plan (+8 more)

### Community 367 - "7. 测试与门禁策略"
Cohesion: 0.25
Nodes (8): 7.1 三类门禁, 7.2 `ugripper` 仓库阶段的测试要求, 7.3 主仓库阶段的测试要求, 7.4 ARM 板 smoke 分级, 7. 测试与门禁策略, `L1` 轻量 smoke, `L2` 主链 smoke, `L3` 交付 smoke

### Community 369 - "9. 配置、安装与 U 盘流程"
Cohesion: 0.25
Nodes (8): 9.1 当前主要配置入口, 9.2 当前默认项, 9.3 U 盘支持内容, 9.4 U 盘同次插入顺序, 9.5 安装脚本与网络行为, 9.6 网线监测行为, 9.7 主包打包约束, 9. 配置、安装与 U 盘流程

### Community 371 - "write_source_manifest.sh"
Cohesion: 0.36
Nodes (5): list_files_under(), list_updater_inputs(), write_source_manifest.sh script, usage(), write_manifest()

### Community 372 - "TODO Schema"
Cohesion: 0.25
Nodes (7): TODO Schema, 任务块约定, 分支规则, 可选字段, 必填字段, 文件结构, 设计目标

### Community 382 - "WriteCalibrationJsonFromHandle"
Cohesion: 0.31
Nodes (10): ostream, CalibrationLooksValid(), CameraModelName(), ATRAK_CAM_MODEL, ATRAK_DISTORTION_MODEL, DistortionModelName(), WriteCalibrationJsonFromHandle(), WriteCameraJson() (+2 more)

### Community 383 - "test_audio_coordinator.cc"
Cohesion: 0.09
Nodes (20): AudioCoordinatorTest, audio_player_started, StopAudioPlayer, audio_player_started(), StartsPlayerAndSendsCommand, SupportsInjectedAudioCommandPort, path, runtime::AudioCommandPort (+12 more)

### Community 384 - "test_stereo_session_client.cc"
Cohesion: 0.09
Nodes (21): string, StereoSessionStatusSnapshot, active_episode_dir, finalize_pending, has_last_session, last_finalize_error, last_finalized_episode_dir, last_session_episode_dir (+13 more)

### Community 385 - "ugripper_shell_common.sh"
Cohesion: 0.29
Nodes (3): ugripper_shell_common.sh script, ugripper_resolve_existing_path(), ugripper_resolve_first_existing()

### Community 386 - "CalculateCrc16Modbus"
Cohesion: 0.13
Nodes (8): span, atomic, ugripper(), mutex, CalculateCrc16Modbus(), optional, TryGetEncoderResponseFrameLength(), VerifyCrc16Modbus()

### Community 389 - "GripperExtrinsicsBlock"
Cohesion: 0.25
Nodes (8): GripperExtrinsicsBlock, baselineNorm, reserved0, reserved1, tIcCam0ToImu0, tIcCam1ToImu0, timeshiftCam0ToImu0, timeshiftCam1ToImu0

### Community 390 - "ShutdownRequestPort"
Cohesion: 0.29
Nodes (6): string, ShutdownRequestPort, RequestAction, WaitForActionResult, ugripper(), CreateFileShutdownRequestPort()

### Community 391 - "board_gripper_hmi_active_check.sh"
Cohesion: 0.39
Nodes (6): confirm_visual_step(), record_visual_result(), REQUIRE_IO_TOTAL_ARGS, run_direct_hmi_step(), board_gripper_hmi_active_check.sh script, usage()

### Community 392 - "3. `ugripper` 当前相机代码的主要问题"
Cohesion: 0.29
Nodes (7): 3.1 `CameraConfig` 责任过重, 3.2 相机“类”其实还是 pipeline 类, 3.3 同一份代码里混了两套采集模型, 3.4 `record_runtime` 对 camera identity 有大量硬编码, 3.5 timing 元信息获取方式耦合到输出日志格式, 3.6 stereo 相机已经暴露出“单个相机不是最小单位”, 3. `ugripper` 当前相机代码的主要问题

### Community 393 - "23.2 推荐测试分层"
Cohesion: 0.29
Nodes (7): 23.1 Phase 1 最小验收集, 23.2 推荐测试分层, 23.3 验收标准, 23. 测试与验收策略, 单元测试, 无硬件冒烟测试, 组件测试

### Community 394 - "28. 建议的拆分 PR 顺序"
Cohesion: 0.29
Nodes (7): 28. 建议的拆分 PR 顺序, PR 1：抽 `SubprocessHandle` 和 `ProcessSupervisor`, PR 2：抽 `AudioCoordinator` 和 `StereoSessionClient`, PR 3：抽 `HmiController`, PR 4：抽 `HealthMonitor`, PR 5：抽 `RecordingOrchestrator`, PR 6：收成 `RuntimeApp`

### Community 395 - "2. `pp_main/test` 里值得复用的设计"
Cohesion: 0.29
Nodes (7): 2.1 单独的测试工程入口, 2.2 测试目录镜像源码模块, 2.3 测试目标链接库，不链接应用壳, 2.4 测试资产和测试代码一起维护, 2.5 每个测试目标都很小, 2.6 复用原则，不照抄构建细节, 2. `pp_main/test` 里值得复用的设计

### Community 396 - "3. `ugripper` 当前阻碍测试的核心问题"
Cohesion: 0.29
Nodes (7): 3.1 当前基本没有正式测试拓扑, 3.2 现有测试更像 smoke / tool, 3.3 可执行程序承担了过多逻辑, 3.4 缺少明确的系统适配层, 3.5 配置语义分散，导致测试断点不稳定, 3.6 硬件协议代码和业务代码耦合太深, 3. `ugripper` 当前阻碍测试的核心问题

### Community 397 - "7. 推荐的测试分层"
Cohesion: 0.14
Nodes (13): 7.1 Unit Test, 7.2 Component Test, 7.3 Hardware Test, 7.4 Field Test, 7.5 本地与 CI 运行矩阵, 7.6 组件测试里的 fake 组合示例, 7. 推荐的测试分层, 9.1 顶层增加测试开关 (+5 more)

### Community 399 - "3. 重构原则"
Cohesion: 0.29
Nodes (7): 3.1 先分层，再美化命名, 3.2 目录表达职责，manifest 决定交付, 3.3 先 freeze，再 archive, 3.4 Shell 只做流程编排, 3.5 先做行为等价迁移，不做业务语义变化, 3.6 先统一职责，再考虑统一格式, 3. 重构原则

### Community 400 - "7. 重构阶段"
Cohesion: 0.29
Nodes (7): 7. 重构阶段, Phase 0：架构建模, Phase 1：manifest 收口, Phase 2：温和目录收口, Phase 3：冻结区处理, Phase 4：脚本拆分, Phase 5：主链职责收口

### Community 402 - "Phase 2：建立最小 `src/utils`"
Cohesion: 0.29
Nodes (7): Phase 2：建立最小 `src/utils`, 不做的事, 依赖约束, 完成标准, 测试策略, 目标, 第一批内容

### Community 404 - "boot_check_install.sh"
Cohesion: 0.43
Nodes (3): install_deb_with_log(), log(), boot_check_install.sh script

### Community 406 - "Version Test Focus"
Cohesion: 0.29
Nodes (6): Version Test Focus, 固定流程, 测试信息提取, 筛选与分类, 输入, 输出约束

### Community 407 - "validate-episode-data"
Cohesion: 0.29
Nodes (6): validate-episode-data, 固定执行顺序, 建议重点补充的异常类型, 资源使用, 输出要求, 默认检查范围

### Community 419 - "`pp_main` 合并后 `ugripper` 测试结构与当前结果简表"
Cohesion: 0.29
Nodes (6): 1. 测试结构, 2. 主要测试项, 3. 当前结果快照, 4. 现在可以怎么理解这些结果, 5. 推荐把它和哪份文档一起看, `pp_main` 合并后 `ugripper` 测试结构与当前结果简表

### Community 420 - "5. 响应约定"
Cohesion: 0.29
Nodes (7): 5.1 通用状态响应, 5.2 读 SN 响应, 5.3 读标定参数响应, 5. 响应约定, code:text (A5 A5 <token> <status> 00 00 A5 XOR), code:text (A5 A5 <16-byte sn chunk> XOR), code:text (A5 A5 <seq> <16-byte chunk> XOR)

### Community 426 - "RunControlLoop"
Cohesion: 0.17
Nodes (7): ConsumeStopRequested(), thread, FaysEventLog(), FaysReadableTimestamp(), main(), ReadEnvUInt64(), RunControlLoop()

### Community 430 - "install_deb_to_arm_target.sh"
Cohesion: 0.48
Nodes (5): require_tool(), scp_cmd(), install_deb_to_arm_target.sh script, ssh_cmd(), usage()

### Community 431 - "open_foxglove_episode.sh"
Cohesion: 0.43
Nodes (4): print_topics(), require_tool(), open_foxglove_episode.sh script, usage()

### Community 432 - "verify_arm_board_install.sh"
Cohesion: 0.62
Nodes (6): require_sudo(), run(), run_allow_fail(), run_privileged(), run_privileged_allow_fail(), verify_arm_board_install.sh script

### Community 434 - "FaysImuSample"
Cohesion: 0.29
Nodes (7): FaysImuSample, ax, ay, az, gx, gy, gz

### Community 437 - "GripperCalibrationHeader"
Cohesion: 0.29
Nodes (7): GripperCalibrationHeader, dataFormatVersion, headerSize, magic, payloadSize, reserved, validFields

### Community 438 - "GripperImuBlock"
Cohesion: 0.29
Nodes (7): GripperImuBlock, accelerometerNoiseDensityDiscrete, accelerometerRandomWalk, gyroscopeNoiseDensityDiscrete, gyroscopeRandomWalk, reserved, updateRate

### Community 439 - "FourButtonSnapshot"
Cohesion: 0.25
Nodes (8): PhysicalButton, ButtonActionRouter::SnapshotMask(), ButtonMask(), FourButtonSnapshot, left_down, left_up, right_down, right_up

### Community 440 - "TactileCalibrationTarget"
Cohesion: 0.29
Nodes (7): TactileCalibrationTarget, cameraName, devicePath, jsonPath, sensorSlot, serialPlaceholder, side

### Community 442 - "board_gripper_hmi_link_stress.sh"
Cohesion: 0.52
Nodes (6): collect_snapshot(), restore_service(), board_gripper_hmi_link_stress.sh script, SIDES, stop_sensor_probe(), usage()

### Community 445 - "4. 重构目标：不是统一成一个基类，而是拆成三层"
Cohesion: 0.33
Nodes (6): 4.1 第一层：相机描述层, 4.2 第二层：设备与采集层, 4.3 第三层：录制会话层, 4. 重构目标：不是统一成一个基类，而是拆成三层, code:cpp (struct CameraDeviceConfig {), code:cpp (struct RecordConfig {)

### Community 446 - "19. `RecordingOrchestrator` 状态机"
Cohesion: 0.33
Nodes (6): 19.1 状态列表, 19.2 输入事件列表, 19.3 状态转移表, 19.4 幂等约束, 19.5 回滚规则, 19. `RecordingOrchestrator` 状态机

### Community 447 - "5. 针对已知问题的专项补测清单"
Cohesion: 0.33
Nodes (6): 5.1 Sensor 录制丢帧, 5.2 相机数据流损坏, 5.3 Gripper 控制丢控制帧, 5.4 HMI 灯效异常, 5.5 按键失灵, 5. 针对已知问题的专项补测清单

### Community 448 - "6. 最终目标"
Cohesion: 0.33
Nodes (6): 6.1 最终交付形态, 6.2 问题闭环目标, 6.3 规范化建设目标, 6.4 更全面覆盖的构建原则, 6.5 建议补充的规范化资产, 6. 最终目标

### Community 450 - "4.2 日志规则"
Cohesion: 0.33
Nodes (6): 4.2 日志规则, C++ 最终规则, code:text ([INFO] [module] ...), Shell / Python 过渡规则, 与 `pp_main` logger 的对齐方式, 日志改造顺序

### Community 451 - "4.4 utils 规则"
Cohesion: 0.33
Nodes (6): 4.4 utils 规则, 哪些函数先不要急着抽, 哪些函数适合先收进 utils, 多线程与调度能力约束, 第一批只收最小 utils, 通信与控制面通道约束

### Community 453 - "Phase 5：收口低风险 helper"
Cohesion: 0.33
Nodes (6): Phase 5：收口低风险 helper, 优先迁移, 完成标准, 暂缓迁移, 测试策略, 目标

### Community 454 - "Phase 6：Camera 抽象收口"
Cohesion: 0.33
Nodes (6): Phase 6：Camera 抽象收口, 参考与边界, 完成标准, 测试策略, 目标, 要拆开的职责

### Community 455 - "monitor_network.sh"
Cohesion: 0.53
Nodes (4): arm_monitor(), is_upgrade_in_progress(), restart_ugripper(), monitor_network.sh script

### Community 458 - "auto_release_deb.sh"
Cohesion: 0.53
Nodes (4): generate_current_manifest(), is_full_build_trigger_file(), auto_release_deb.sh script, usage()

### Community 459 - "auto-release-deb"
Cohesion: 0.33
Nodes (5): auto-release-deb, 执行步骤, 输出约束, 适用场景, 默认规则

### Community 460 - "Episode 校验清单"
Cohesion: 0.33
Nodes (5): 1.1 推荐附加检查, 1. 必查文件, 2. 默认阈值建议, 3. 常见异常模式, Episode 校验清单

### Community 467 - "Ugripper 错误码对照表"
Cohesion: 0.33
Nodes (5): Ugripper 错误码对照表, 后续错误兼容, 工程备注, 当前映射, 错误码分类

### Community 468 - "TODO List"
Cohesion: 0.33
Nodes (5): Active Tasks, code:toml (id = "TASK-001"), TODO List, 新任务模板（复制后记得补 `<!-- TODO_HUB_TASK -->` 标记）, 规则

### Community 471 - "GstRecorder"
Cohesion: 0.33
Nodes (4): pid_t, GstRecorder, childPid_, pipeFd_

### Community 472 - "GripperRgbCameraBlock"
Cohesion: 0.33
Nodes (6): GripperRgbCameraBlock, cameraModelEnum, distortionCoefficients, intrinsics, reserved, resolution

### Community 473 - "board_camera_stress.sh"
Cohesion: 0.40
Nodes (4): CPU_PIDS, EXTRA_FLAGS, board_camera_stress.sh script, usage()

### Community 474 - "board_gripper_hmi_fault_soak.sh"
Cohesion: 0.40
Nodes (5): LOOP_ARGS, MAX_HEALTH_FAULT_ARGS, board_gripper_hmi_fault_soak.sh script, SUMMARY_ARGS, usage()

### Community 475 - "board_gripper_hmi_log_check.sh"
Cohesion: 0.40
Nodes (5): GRIPPER_ARGS, HMI_ARGS, REQUIRE_IO_TOTAL_ARGS, board_gripper_hmi_log_check.sh script, usage()

### Community 476 - "board_hmi_high_rate_soak.sh"
Cohesion: 0.67
Nodes (5): collect_snapshot(), restore_service(), board_hmi_high_rate_soak.sh script, stop_sensor(), usage()

### Community 477 - "board_hmi_self_timeout_soak.sh"
Cohesion: 0.67
Nodes (5): collect_snapshot(), restore_service(), board_hmi_self_timeout_soak.sh script, stop_process(), usage()

### Community 478 - "board_hmi_strict_reply_soak.sh"
Cohesion: 0.67
Nodes (5): collect_snapshot(), restore_service(), board_hmi_strict_reply_soak.sh script, stop_sensor(), usage()

### Community 479 - "board_tactile_ffmpeg_parallel_soak.sh"
Cohesion: 0.60
Nodes (5): capture_snapshot(), cleanup(), restore_service(), board_tactile_ffmpeg_parallel_soak.sh script, usage()

### Community 480 - "board_tactile_ffmpeg_streamon_soak.sh"
Cohesion: 0.60
Nodes (5): capture_snapshot(), cleanup(), restore_service(), board_tactile_ffmpeg_streamon_soak.sh script, usage()

### Community 481 - "10. 哪些设计可以直接借鉴，哪些不能直接照搬"
Cohesion: 0.40
Nodes (5): 10.1 可以直接借鉴, 10.2 不能直接照搬, 10.3 需要选择性复用, 10.4 当前阶段不建议做的事, 10. 哪些设计可以直接借鉴，哪些不能直接照搬

### Community 482 - "9. 建议的迁移顺序"
Cohesion: 0.40
Nodes (5): 9. 建议的迁移顺序, 第 1 步：先抽公共类型和配置层，不改运行时行为, 第 2 步：让 `record_runtime` 改为依赖 `CameraRegistry`, 第 3 步：把 recorder 类从“相机类”改成“backend 类”, 第 4 步：评估与 `pp_main` 设备层的收敛点

### Community 483 - "2. `pp_main` 当前可复用的规范基线"
Cohesion: 0.40
Nodes (5): 2.1 目录与文件命名, 2.2 C++ 标识符命名, 2.3 YAML 入口和结构, 2.4 测试里的 YAML 资产组织, 2. `pp_main` 当前可复用的规范基线

### Community 484 - "11. 推荐迁移阶段"
Cohesion: 0.40
Nodes (5): 11. 推荐迁移阶段, Phase 1：仓内重构，不改主进程边界, Phase 2：控制面向 standalone 模式对齐, Phase 3：逐步并入 `pp_main/standalone`, Phase 4：评估是否继续缩减进程边界

### Community 485 - "12. 重构判断标准"
Cohesion: 0.40
Nodes (5): 12.1 这是“控制逻辑”还是“采集执行逻辑”, 12.2 这是“周期检查”还是“阻塞 I/O”, 12.3 这是“决策”还是“执行”, 12.4 这是“顶层单实例问题”还是“子进程监督问题”, 12. 重构判断标准

### Community 486 - "20. shell / systemd / runtime 边界"
Cohesion: 0.40
Nodes (5): 20.1 推荐边界原则, 20.2 现状逻辑如何重画, 20.3 对 `run_record.sh` 的建议, 20.4 为什么不建议一开始彻底删 shell, 20. shell / systemd / runtime 边界

### Community 495 - "6. 录制生命周期"
Cohesion: 0.40
Nodes (5): 6.1 开始录制, 6.2 相机链路, 6.3 传感器链路, 6.4 停止录制, 6. 录制生命周期

### Community 507 - "Q: 以主摄链路为例，讲解从采集线程拉起到文件构建构的过程"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 以主摄链路为例，讲解从采集线程拉起到文件构建构的过程, Source Nodes

### Community 508 - "Q: 当前的事件是基于已有的日志，以及项目实际情况做的全覆盖的事件类型吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 当前的事件是基于已有的日志，以及项目实际情况做的全覆盖的事件类型吗, Source Nodes

### Community 509 - "Q: 基于这个项目给我介绍一下usb总线，我没学过"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 基于这个项目给我介绍一下usb总线，我没学过, Source Nodes

### Community 510 - "Q: 我现在同步在执行错误码修改的phase3,4,该日志进行修改会影响吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 我现在同步在执行错误码修改的phase3,4,该日志进行修改会影响吗, Source Nodes

### Community 511 - "Q: 开始分析status log,这个文件的目的是给使用数采结果的算法员，能够获得该episode的软硬件环境，保留核心信息"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 开始分析status log,这个文件的目的是给使用数采结果的算法员，能够获得该episode的软硬件环境，保留核心信息, Source Nodes

### Community 512 - "Q: 介绍一下，一次episode结束会产生的日志"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 介绍一下，一次episode结束会产生的日志, Source Nodes

### Community 513 - "Q: 该项目debug是用gdb吗？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 该项目debug是用gdb吗？, Source Nodes

### Community 514 - "Q: 更新status的指责：用于给另一个进程的通信，比如一个手机程序要实时获取rk采集状态，需要读取这个status.json status的内容是各个硬件的状态，比如世界相机，fays等，目的是如果报错了能快速获取错误位置，再次重构status的内容"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 更新status的指责：用于给另一个进程的通信，比如一个手机程序要实时获取rk采集状态，需要读取这个status.json status的内容是各个硬件的状态，比如世界相机，fays等，目的是如果报错了能快速获取错误位置，再次重构status的内容, Source Nodes

### Community 515 - "Q: rk板里面/dev/shm/ugripper都是什么文件"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: rk板里面/dev/shm/ugripper都是什么文件, Source Nodes

### Community 516 - "Q: 这是我之前和ai的对话，基于当前本地你可以读取的这个项目，继续回答我的问题"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 这是我之前和ai的对话，基于当前本地你可以读取的这个项目，继续回答我的问题, Source Nodes

### Community 517 - "Q: 是不是走其中一个硬件的完整数据链路和开发过程？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 是不是走其中一个硬件的完整数据链路和开发过程？, Source Nodes

### Community 518 - "Q: 但是这些完整链路是不是当前项目已经实现了？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 但是这些完整链路是不是当前项目已经实现了？, Source Nodes

### Community 519 - "Q: 目前我还在进行日志重构和错误码重构，给我这三个优化项目的执行顺序"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 目前我还在进行日志重构和错误码重构，给我这三个优化项目的执行顺序, Source Nodes

### Community 520 - "Q: 基于我的简历模板，如何把 Ugripper 项目写成符合实习生贡献尺度、突出完整数据链路、架构优化和性能提升的嵌入式 Linux 项目经历？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 基于我的简历模板，如何把 Ugripper 项目写成符合实习生贡献尺度、突出完整数据链路、架构优化和性能提升的嵌入式 Linux 项目经历？, Source Nodes

### Community 521 - "Q: 基于 Ugripper 串口 encoder 数据链路扫盲并准备面试问题"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 基于 Ugripper 串口 encoder 数据链路扫盲并准备面试问题, Source Nodes

### Community 522 - "Q: Ugripper encoder 串口链路应该优化哪些数据指标，对应方案有哪些？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: Ugripper encoder 串口链路应该优化哪些数据指标，对应方案有哪些？, Source Nodes

### Community 523 - "Q: 目前phase4已经全部完成，检查日志更新条件是否成熟"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 目前phase4已经全部完成，检查日志更新条件是否成熟, Source Nodes

### Community 524 - "Q: 关于错误码在日志的展现形式：以目前已经重构好的错误码为准；进入R1需要创建的两个文档作用是什么"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 关于错误码在日志的展现形式：以目前已经重构好的错误码为准；进入R1需要创建的两个文档作用是什么, Source Nodes

### Community 525 - "Q: 现有项目生成的4个mcap文件，以sensor_left.mcap和fays_data_left.mcap为典型，这两个mcap文件结构如何定义，有什么字段，每个字段包含什么数据，数据怎么来，解释数据内容"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 现有项目生成的4个mcap文件，以sensor_left.mcap和fays_data_left.mcap为典型，这两个mcap文件结构如何定义，有什么字段，每个字段包含什么数据，数据怎么来，解释数据内容, Source Nodes

### Community 526 - "Q: 解释sensor_left.mcap的message中每个字段的含义"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 解释sensor_left.mcap的message中每个字段的含义, Source Nodes

### Community 527 - "Q: mcap文件生成之后，256 KiB Chunk、LZ4压缩、Schema/Channel记录、Footer、不生成Message Index是什么，开与不开有什么影响"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: mcap文件生成之后，256 KiB Chunk、LZ4压缩、Schema/Channel记录、Footer、不生成Message Index是什么，开与不开有什么影响, Source Nodes

### Community 528 - "Q: chunk可以设置压缩前大小，现在是256k；LZ4是压缩协议还是压缩选项？还有别的选择吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: chunk可以设置压缩前大小，现在是256k；LZ4是压缩协议还是压缩选项？还有别的选择吗, Source Nodes

### Community 529 - "Q: 现在说要我做力传感器的接入，数据链路构建（使用现有的sensor_encoder链路，用串口通信+CRC校验），以及传感器数据的标定（力的大小）"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 现在说要我做力传感器的接入，数据链路构建（使用现有的sensor_encoder链路，用串口通信+CRC校验），以及传感器数据的标定（力的大小）, Source Nodes

### Community 530 - "Q: 如果要是各个传感器数据同时使用训练，mcap的时间戳需要对齐吗/项目里有对齐吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 如果要是各个传感器数据同时使用训练，mcap的时间戳需要对齐吗/项目里有对齐吗, Source Nodes

### Community 531 - "Q: 解析sensor_code链路，如果可以，从底层寄存器数据读取开始，到串口数据传输的参数配置，板端解包过程，数据读取过程，打包为mcap的过程（更正：sensor_encoder）"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 解析sensor_code链路，如果可以，从底层寄存器数据读取开始，到串口数据传输的参数配置，板端解包过程，数据读取过程，打包为mcap的过程（更正：sensor_encoder）, Source Nodes

### Community 532 - "Q: pp-main docker里面是否有一个”pp main“ CPP框架"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: pp-main docker里面是否有一个”pp main“ CPP框架, Source Nodes

### Community 533 - "Q: Ugripper在容器内编译，需要使用的pp_main框架呢"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: Ugripper在容器内编译，需要使用的pp_main框架呢, Source Nodes

### Community 534 - "Q: 检查 /home/dm/Doc/ugripper_runtime_status_design_v1.md 中 status.log 方案当前构建进入到哪一步"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 检查 /home/dm/Doc/ugripper_runtime_status_design_v1.md 中 status.log 方案当前构建进入到哪一步, Source Nodes

### Community 535 - "Q: 是否可以找到完整的PP_main仓库"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 是否可以找到完整的PP_main仓库, Source Nodes

### Community 536 - "Q: 当前docker是怎么构建的？里面的pp_main组件是怎么下载的？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 当前docker是怎么构建的？里面的pp_main组件是怎么下载的？, Source Nodes

### Community 537 - "Q: README.md里面不是说交叉编译容器需要 /home/dm/proj/pp-main/standalone/UGripper 文件夹吗？本地是否存在"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: README.md里面不是说交叉编译容器需要 /home/dm/proj/pp-main/standalone/UGripper 文件夹吗？本地是否存在, Source Nodes

### Community 538 - "Q: 为什么当时构建的时候没有按照README走"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 为什么当时构建的时候没有按照README走, Source Nodes

### Community 539 - "Q: 如果不在 pp_main 上构建，Harbor 镜像到 pp-arm-dev 容器、挂载 ugripper-1、使用 ARM toolchain 编译并打 deb 的过程写在哪里？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 如果不在 pp_main 上构建，Harbor 镜像到 pp-arm-dev 容器、挂载 ugripper-1、使用 ARM toolchain 编译并打 deb 的过程写在哪里？, Source Nodes

### Community 540 - "Q: pp-arm-dev 这个名称在哪里沿用的？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: pp-arm-dev 这个名称在哪里沿用的？, Source Nodes

### Community 541 - "Q: ego和umi在项目里是什么意思"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: ego和umi在项目里是什么意思, Source Nodes

### Community 542 - "Q: /home/dm/Code/ugripper-1/这里是具体的蔬菜项目，比较成熟，已有传感器链路，可以参考这个吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: /home/dm/Code/ugripper-1/这里是具体的蔬菜项目，比较成熟，已有传感器链路，可以参考这个吗, Source Nodes

### Community 543 - "Q: 分析原有日志的问题，以及新日志的优越性，这段总结是否正确"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 分析原有日志的问题，以及新日志的优越性，这段总结是否正确, Source Nodes

### Community 544 - "Q: 有几个文件的当前功能分支的版本对比develop-aci修改过大，分析原因： run_fays_stereo_daemon.sh"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 有几个文件的当前功能分支的版本对比develop-aci修改过大，分析原因： run_fays_stereo_daemon.sh, Source Nodes

### Community 545 - "Q: log_event和log_adapter需要被高度复用，目前功能分支 的head具有大量的代码冗余，分析原因以及优化方案，优化方向就是代码的复用，包括C++类的复用等方式"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: log_event和log_adapter需要被高度复用，目前功能分支 的head具有大量的代码冗余，分析原因以及优化方案，优化方向就是代码的复用，包括C++类的复用等方式, Source Nodes

### Community 546 - "Q: 1. audio的问题不管 2. 这样会改什么文件？新增什么文件？能够实现降低代码冗余度的效果吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 1. audio的问题不管 2. 这样会改什么文件？新增什么文件？能够实现降低代码冗余度的效果吗, Source Nodes

### Community 547 - "Q: 基于当前已经做好的串口传输编码器数据链路作为成果 /home/dm/Code/ugripper-1/，构建简历内容"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 基于当前已经做好的串口传输编码器数据链路作为成果 /home/dm/Code/ugripper-1/，构建简历内容, Source Nodes

### Community 548 - "Q: 这几个才减少十几行？"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 这几个才减少十几行？, Source Nodes

### Community 549 - "Q: 之前说的将日志业务代码剥离出主业务文件没有做吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 之前说的将日志业务代码剥离出主业务文件没有做吗, Source Nodes

### Community 550 - "Q: 所以你是没有按照执行文件走吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 所以你是没有按照执行文件走吗, Source Nodes

### Community 551 - "Q: 所以应该回撤到阶段B,执行阶段B到E是吗"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 所以应该回撤到阶段B,执行阶段B到E是吗, Source Nodes

### Community 552 - "Q: runtime_domain.cc 的 sensor_args 是什么含义"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: runtime_domain.cc 的 sensor_args 是什么含义, Source Nodes

### Community 553 - "Q: 前面精简版和详细版的简历都没有数据成果，无法给简历成果做支撑，如果要添加数据作为实习成果，应该怎么测试"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 前面精简版和详细版的简历都没有数据成果，无法给简历成果做支撑，如果要添加数据作为实习成果，应该怎么测试, Source Nodes

### Community 554 - "Q: 编码器链路的worker做什么"
Cohesion: 0.40
Nodes (4): Answer, Outcome, Q: 编码器链路的worker做什么, Source Nodes

### Community 558 - "build_arm_deb_in_pp_arm_dev.sh"
Cohesion: 0.60
Nodes (4): is_container_runtime(), PKG_CONFIG_PATH, build_arm_deb_in_pp_arm_dev.sh script, usage()

### Community 559 - "ugripper_restore_usb"
Cohesion: 0.70
Nodes (4): ugripper_restore_usb script, prepare_gpio(), try_start_bluetooth_gatt(), try_stop_bluetooth_gatt()

### Community 561 - "board_hmi_record_start_matrix.sh script"
Cohesion: 0.70
Nodes (4): collect_snapshot(), restore_service(), board_hmi_record_start_matrix.sh script, usage()

### Community 562 - "board_service_episode_loop_check.sh"
Cohesion: 0.60
Nodes (4): KNOWN_EPISODES, board_service_episode_loop_check.sh script, usage(), wait_for_episode_ready()

### Community 563 - "board_service_integration_check.sh"
Cohesion: 0.50
Nodes (4): REQUIRE_GRIPPER_IO_TOTAL_ARGS, board_service_integration_check.sh script, SUMMARY_ARGS, usage()

### Community 564 - "camera_crash_capture.sh"
Cohesion: 0.60
Nodes (3): flush_loop(), log_note(), camera_crash_capture.sh script

### Community 567 - "4. 两种模式的根本差异"
Cohesion: 0.50
Nodes (4): 4.1 `pp_main` 的主要问题域, 4.2 `ugripper` 当前的主要问题域, 4.3 直接套 `pp_main` 设计为什么不够, 4. 两种模式的根本差异

### Community 568 - "5. 后续重构的总原则"
Cohesion: 0.50
Nodes (4): 5.1 不要先追求“大一统单进程”, 5.2 推荐方案：少量关键多进程 + 进程内学习 standalone 模式, 5.3 控制面与采集面分离, 5. 后续重构的总原则

### Community 569 - "7. 哪些边界建议保留多进程"
Cohesion: 0.50
Nodes (4): 7.1 建议保留独立进程的模块, 7.2 不建议早期强行合并的内容, 7.3 更适合收口到单一 runtime 进程的部分, 7. 哪些边界建议保留多进程

### Community 570 - "8. 从 `pp_main` 可直接复用什么"
Cohesion: 0.50
Nodes (4): 8.1 可以直接复用的设计思路, 8.2 可以比较直接引入的组件, 8.3 需要选择性复用的设计, 8. 从 `pp_main` 可直接复用什么

### Community 576 - "collect_debug_context.sh"
Cohesion: 0.83
Nodes (3): section(), collect_debug_context.sh script, show_file()

### Community 577 - "install-arm-deb"
Cohesion: 0.50
Nodes (3): install-arm-deb, Notes, Workflow

### Community 578 - "<旧版本> → <新版本> 测试关注点"
Cohesion: 0.50
Nodes (3): <功能名称>, 待确认项, <旧版本> → <新版本> 测试关注点

### Community 580 - "allocate_worktree.sh"
Cohesion: 0.83
Nodes (3): apply_dirty_snapshot(), copy_untracked_snapshot(), allocate_worktree.sh script

### Community 582 - "TEST"
Cohesion: 0.50
Nodes (3): DefaultBindingUsesBootScopedSharedMemory, EgoBindingConfigTest, TEST()

### Community 591 - "Q: 原来 Ugripper 是否依赖 pp_main，当前交叉编译容器是否把 pp_main 组件单独放在 Harbor？"
Cohesion: 0.50
Nodes (3): Answer, Outcome, Q: 原来 Ugripper 是否依赖 pp_main，当前交叉编译容器是否把 pp_main 组件单独放在 Harbor？

### Community 592 - "Q: Harbor 中的工具链和组件是否大量采用 pp_main 的内容？"
Cohesion: 0.50
Nodes (3): Answer, Outcome, Q: Harbor 中的工具链和组件是否大量采用 pp_main 的内容？

### Community 593 - "Q: pp_main 具体在哪里影响了 Ugripper？通过什么方式影响？"
Cohesion: 0.50
Nodes (3): Answer, Outcome, Q: pp_main 具体在哪里影响了 Ugripper？通过什么方式影响？

### Community 594 - "Q: 开发过程中的项目框架、线程池、进程管理是否由 pp_main 影响 Ugripper？"
Cohesion: 0.50
Nodes (3): Answer, Outcome, Q: 开发过程中的项目框架、线程池、进程管理是否由 pp_main 影响 Ugripper？

### Community 596 - "prerm"
Cohesion: 0.83
Nodes (3): prerm script, force_cleanup_recording_processes(), stop_service_fast()

### Community 598 - "restore_service"
Cohesion: 0.83
Nodes (3): cleanup(), restore_service(), board_encoder_cmd_ack_latency.sh script

### Community 599 - "board_sensor_smoke.sh"
Cohesion: 0.67
Nodes (3): board_sensor_smoke.sh script, SUMMARY_ARGS, usage()

### Community 600 - "validate"
Cohesion: 0.67
Nodes (3): main(), Path, validate()

### Community 601 - "test_ch9344_symlink_name.sh"
Cohesion: 0.83
Nodes (3): check_group(), fail(), test_ch9344_symlink_name.sh script

### Community 603 - "usb_updater_build.sh"
Cohesion: 0.50
Nodes (3): BUILD_DIR, OUTPUT_DIR, usb_updater_build.sh script

### Community 634 - "stereo_session_port.h"
Cohesion: 0.13
Nodes (17): AudioCommandPort, HasCommandChannel, IsBackendReady, ResetReadyState, SendCommand, StereoSessionPort(), LoadStatus, ResetSessionState (+9 more)

## Ambiguous Edges - Review These
- `Ugripper V2` → `AudioCommandPort`  [AMBIGUOUS]
  docs/archive/2026-refactor-history/baseline/config-entrypoints.md · relation: conceptually_related_to
- `TODO List` → `Agent Overview`  [AMBIGUOUS]
  docs/todo_list.md · relation: conceptually_related_to
- `Ugripper Current Status Overview` → `pp_main 合并后 ugripper 正式测试方案`  [AMBIGUOUS]
  docs/agent/current-status.md · relation: conceptually_related_to
- `StereoSessionPort` → `CAMERA_CODEC`  [AMBIGUOUS]
  docs/archive/2026-refactor-history/baseline/config-entrypoints.md · relation: conceptually_related_to

## Knowledge Gaps
- **2666 isolated node(s):** `ensure_monitor.sh script`, `launch_agent_session.sh script`, `monitor_tasks.sh script`, `relay_command.sh script`, `report_progress.sh script` (+2661 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **42 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Work-memory lessons

**Preferred sources** — corroborated by past sessions; start here.
- `EncoderDriver` (11× useful, score=9.662153654)
- `PendingWriteQueue` (7× useful, score=6.083718644)
- `BatchTimestampSmoothingState` (6× useful, score=5.259745217)
- `FaysRecorder` (6× useful, score=5.002600455)
- `pp_main standalone mode` (4× useful, score=3.491397181)
- `ros2-humble-arm Container Environment Request` (4× useful, score=3.491397181)
- `ProcessSupervisor` (4× useful, score=3.486514021)
- `RecordingOrchestrator` (4× useful, score=3.285262725) _(code changed — re-verify)_
- `runtime_process.cc` (3× useful, score=2.769937298)
- `SideWriter` (3× useful, score=2.571128516)

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `Ugripper V2` and `AudioCommandPort`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **What is the exact relationship between `TODO List` and `Agent Overview`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **What is the exact relationship between `Ugripper Current Status Overview` and `pp_main 合并后 ugripper 正式测试方案`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **What is the exact relationship between `StereoSessionPort` and `CAMERA_CODEC`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **Why does `empty()` connect `empty` to `CalculateCrc16Modbus`, `record_runtime.cc`, `empty`, `test_audio_coordinator.cc`, `initialize`, `CameraConfig`, `initialize`, `camera_domain.cc`, `runtime_process.cc`, `camera_recorder.cc`, `RecordingOrchestrator`, `TEST`, `TEST`, `StereoSessionClient`, `TEST`, `encoder_driver.cc`, `runtime_domain.cc`, `RunControlLoop`, `GripperHmiProtocol`, `HealthFault`, `main`, `record.cpp`, `currentSteadyMs`, `gripper_hmi_protocol.cc`, `EnvUtilsTest`, `camera_recorder.cc`, `TEST`, `log_fmtter.cc`, `record.cpp`, `writeCalibrationData`, `gripperStateIndexForSide`, `main`, `silenceBeep`, `IntervalTree`, `parse_args`, `gripper_hmi_calibration_data.h`, `main`, `TEST`, `FileLogger`, `fifo_utils.cc`, `check_sensor_mcap.cc`, `TEST`?**
  _High betweenness centrality (0.058) - this node is a cross-community bridge._
- **Why does `mutex` connect `CalculateCrc16Modbus` to `RecordRuntime`, `record_runtime.cc`, `camera_domain.cc`, `GripperHmiDriver`, `camera_recorder.cc`, `string`, `MainCameraRecorder`, `FaysDataLogger`, `FaysRecorder`, `StereoCameraTrack`, `StereoSessionRecorder`, `ShellCameraRecorder`, `CamTsQueue`, `RecordingHarness`, `VideoFrameQueue`, `record.cpp`, `StereoWarmupCapture`, `EncoderDriver`, `TEST`, `FileLogger`?**
  _High betweenness centrality (0.038) - this node is a cross-community bridge._
- **Why does `RecordRuntime` connect `RecordRuntime` to `CalculateCrc16Modbus`, `runtime_process.cc`, `empty`, `ShutdownRequestPort`, `empty`, `string`, `StereoSessionClient`, `BufferedFifoLineReader`, `TEST`, `HealthFault`, `AudioCoordinator`, `currentSteadyMs`, `runtime_domain.cc`, `ButtonChordDefinition`, `HealthMonitor`, `ProcessSupervisor`, `RecordRuntimeOptions`, `gripperStateIndexForSide`, `SubprocessHandle`, `HmiController`?**
  _High betweenness centrality (0.030) - this node is a cross-community bridge._