#include "record_runtime.h"
#include "record_runtime/runtime_episode_internal.h"
#include "record_runtime/runtime_split_internal.h"
#include "record_runtime/runtime_support_internal.h"
#include "main_camera_calibration_data.h"
#include "record_runtime/error_code_mapping.h"
#include "utils/ugripper_logger.h"
#include "record_runtime/gripper_refresh_logic.h"
#include "utils/env_utils.h"
#include "utils/file_utils.hpp"
#include "utils/time_utils.h"
#include "common/encoder_health.h"

#include <mcap/reader.hpp>
#include <nlohmann/json.hpp>
#include <uuid/uuid.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <utility>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;
using namespace DA::Ugripper::detail;

namespace DA::Ugripper {

GripperLedEffect RecordRuntime::makeLedEffect(LedState state, double progress)
{
    switch (state)
    {
    case RecordRuntime::LedState::BootInit:
        return {GripperLedEffectState::BootInit, progress};
    case RecordRuntime::LedState::Init:
        return {GripperLedEffectState::Init, progress};
    case RecordRuntime::LedState::Writing:
        return {GripperLedEffectState::Writing, progress};
    case RecordRuntime::LedState::WaitStorage:
        return {GripperLedEffectState::WaitStorage, progress};
    case RecordRuntime::LedState::Ready:
        return {GripperLedEffectState::Ready, progress};
    case RecordRuntime::LedState::Warning:
        return {GripperLedEffectState::Warning, progress};
    case RecordRuntime::LedState::Recording:
        return {GripperLedEffectState::Recording, progress};
    case RecordRuntime::LedState::Error1:
        return {GripperLedEffectState::Error1, progress};
    case RecordRuntime::LedState::Error2:
        return {GripperLedEffectState::Error2, progress};
    case RecordRuntime::LedState::Error3:
        return {GripperLedEffectState::Error3, progress};
    case RecordRuntime::LedState::Error4:
        return {GripperLedEffectState::Error4, progress};
    case RecordRuntime::LedState::Error5:
        return {GripperLedEffectState::Error5, progress};
    case RecordRuntime::LedState::CalibPre:
        return {GripperLedEffectState::CalibPre, progress};
    case RecordRuntime::LedState::CalibRun:
        return {GripperLedEffectState::CalibRun, progress};
    case RecordRuntime::LedState::CalibDone:
        return {GripperLedEffectState::CalibDone, progress};
    case RecordRuntime::LedState::Exit:
    default:
        return {GripperLedEffectState::Exit, progress};
    }
}

RecordRuntime::LedState RecordRuntime::toLedState(DA::Ugripper::RuntimeLedState state)
{
    switch (state)
    {
    case DA::Ugripper::RuntimeLedState::Init:
        return LedState::Init;
    case DA::Ugripper::RuntimeLedState::Writing:
        return LedState::Writing;
    case DA::Ugripper::RuntimeLedState::Ready:
        return LedState::Ready;
    case DA::Ugripper::RuntimeLedState::Recording:
        return LedState::Recording;
    case DA::Ugripper::RuntimeLedState::Error1:
        return LedState::Error1;
    case DA::Ugripper::RuntimeLedState::Error2:
        return LedState::Error2;
    case DA::Ugripper::RuntimeLedState::Error3:
        return LedState::Error3;
    case DA::Ugripper::RuntimeLedState::Error4:
        return LedState::Error4;
    case DA::Ugripper::RuntimeLedState::Error5:
        return LedState::Error5;
    case DA::Ugripper::RuntimeLedState::CalibPre:
        return LedState::CalibPre;
    case DA::Ugripper::RuntimeLedState::CalibRun:
        return LedState::CalibRun;
    case DA::Ugripper::RuntimeLedState::CalibDone:
        return LedState::CalibDone;
    case DA::Ugripper::RuntimeLedState::Exit:
    default:
        return LedState::Exit;
    }
}

const char *RecordRuntime::ledStateName(LedState state)
{
    switch (state)
    {
    case LedState::BootInit:
        return "BootInit";
    case LedState::Init:
        return "Init";
    case LedState::Writing:
        return "Writing";
    case LedState::WaitStorage:
        return "WaitStorage";
    case LedState::Ready:
        return "Ready";
    case LedState::Warning:
        return "Warning";
    case LedState::Recording:
        return "Recording";
    case LedState::Error1:
        return "Error1";
    case LedState::Error2:
        return "Error2";
    case LedState::Error3:
        return "Error3";
    case LedState::Error4:
        return "Error4";
    case LedState::Error5:
        return "Error5";
    case LedState::CalibPre:
        return "CalibPre";
    case LedState::CalibRun:
        return "CalibRun";
    case LedState::CalibDone:
        return "CalibDone";
    case LedState::Exit:
        return "Exit";
    }
    return "Unknown";
}

const char *RecordRuntime::hmiEventName(DA::Ugripper::HmiEventType event_type)
{
    switch (event_type)
    {
    case DA::Ugripper::HmiEventType::ShortUpPressed:
        return "ShortUpPressed";
    case DA::Ugripper::HmiEventType::ShortDownPressed:
        return "ShortDownPressed";
    case DA::Ugripper::HmiEventType::LongUpPressed:
        return "LongUpPressed";
    case DA::Ugripper::HmiEventType::LongDownPressed:
        return "LongDownPressed";
    case DA::Ugripper::HmiEventType::ShutdownPromptRequested:
        return "ShutdownPromptRequested";
    case DA::Ugripper::HmiEventType::ShutdownRequested:
        return "ShutdownRequested";
    }
    return "Unknown";
}

RecordRuntime::RecordRuntime(RecordRuntimeOptions options)
    : options_(std::move(options))
{
}

RecordRuntime::~RecordRuntime()
{
    Stop();
    stopEgoWarmWorker();
    stopRecording(false, "shutdown");
    stopSystemLogWorker();
    closeRecordControlPipe();
    requestStopBackgroundTactileValidation();
    waitForMainCameraRefreshes();
    egoFeedback_.reset();
    stopAudioPlayer();
    if (ledController_)
    {
        ledController_->stop();
    }
    panelManager_.turnOff();
    panelManager_.disconnect();
}

bool RecordRuntime::Initialize()
{
    deviceSn_ = readEnvValue(options_.envFile, "DEVICE_SN");
    if (deviceSn_.empty())
    {
        deviceSn_ = "noname_device";
    }

    language_ = toLower(readEnvValue(options_.envFile, "UGRIPPER_LANG"));
    if (language_.empty())
    {
        language_ = "zh";
    }

    options_.cameraCodec = toLower(readEnvValue(options_.envFile, "CAMERA_CODEC"));
    if (options_.cameraCodec.empty())
    {
        options_.cameraCodec = "h264";
    }
    if (options_.cameraCodec != "h264" && options_.cameraCodec != "h265")
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "invalid CAMERA_CODEC, fallback to h264: " << options_.cameraCodec << std::endl).str());
        options_.cameraCodec = "h264";
    }

    chestCameraEnabled_ = !isFalseLikeValue(readEnvValue(options_.envFile, kChestCameraEnvKey));
    DM_LOG_INFO("{}", (::DA::utils::LogString() << kChestCameraEnvKey << "="
                         << (chestCameraEnabled_ ? "true" : "false") << std::endl).str());

    stereoEnabled_ = kPackageStereoEnabled;
    DM_LOG_INFO("{}", (::DA::utils::LogString() << kPackageStereoBuildMarker
                         << " PACKAGE_STEREO_ENABLED="
                         << (stereoEnabled_ ? "true" : "false") << std::endl).str());

    perfLogEnabled_ = kPerfLogEnabled;
    g_perfLogEnabled = perfLogEnabled_;
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "UGRIPPER_ENABLE_PERF_LOG="
                         << (perfLogEnabled_ ? "true" : "false") << std::endl).str());

    hardwareVersion_ = readEnvValue(options_.envFile, "HARDWARE_VERSION");
    if (hardwareVersion_.empty())
        hardwareVersion_ = readEnvValue(options_.envFile, "UGRIPPER_HARDWARE_VERSION");
    if (!hardwareVersion_.empty() && hardwareVersion_.front() != 'v' && hardwareVersion_.front() != 'V')
        hardwareVersion_ = "v" + hardwareVersion_;
    if (hardwareVersion_.empty())
    {
        hardwareVersion_ = "v2.5";
    }

    if (options_.gripperPorts.empty())
    {
        options_.gripperPorts.emplace_back("/dev/right_gripper");
        options_.gripperPorts.emplace_back("/dev/left_gripper");
    }

    loadEgoBinding();

    std::error_code error;
    fs::create_directories(options_.audioTempDir, error);

    packageVersion_ = queryPackageVersion("ugripper");
    updaterVersion_ = queryPackageVersion("das-usb-updater");
    if (updaterVersion_ == "unknown")
    {
        updaterVersion_ = queryPackageVersion("ugripper-usb-updater");
    }

    episodeManager_ = std::make_unique<EpisodeManager>(
        options_.diskRoot,
        deviceSn_,
        language_,
        options_.cameraCodec,
        chestCameraEnabled_,
        stereoEnabled_,
        options_.tactileStateDir,
        options_.persistCalibrationFile,
        options_.exampleCalibrationFile,
        options_.fallbackCalibrationFile,
        options_.stereoStatusFile,
        hardwareVersion_,
        packageVersion_,
        updaterVersion_,
        DA::Ugripper::EpisodeManagerDependencies{
            .currentEpochMs = &RecordRuntime::currentEpochMs,
            .fileExistsAndNotEmpty = &RecordRuntime::fileExistsAndNotEmpty,
        });

    for (const auto *field : kDeviceVersionFields) deviceVersions_[field] = "";
    for (const auto &[field, package] : std::array<std::pair<const char *, const char *>, 3>{{
        {"bluetooth_gatt_server_version", "bluetooth-gatt-server"},
        {"databot_device_joint_version", "databot-device-joint"},
        {"device_ota_mender_version", "device-ota-mender"},
    }})
    {
        const auto version = queryPackageVersion(package);
        deviceVersions_[field] = version == "unknown" ? "" : version;
    }
    episodeManagerInitialized_ = false;
    initializeMainCameraRuntimeStates();
    initializeTactileCameraRuntimeStates();
    maintainTactileCameraRuntimeStates();

    if (!panelManager_.connect(options_.gripperPorts))
    {
        DM_LOG_ERROR_CODE("UGR-40-01", "{}", (::DA::utils::LogString() << "failed to connect any gripper HMI port" << std::endl).str());
        return false;
    }

    ledController_ = std::make_unique<HmiLedController>(&panelManager_);
    ledController_->start();
    setLedState(LedState::BootInit);

    hmiController_ = std::make_unique<DA::Ugripper::HmiController>(
        DA::Ugripper::HmiControllerOptions{
            .press_debounce_ms = kButtonPressDebounceMs,
            .release_debounce_ms = kButtonReleaseDebounceMs,
            .action_debounce_ms = kActionDebounceMs,
            .long_press_threshold_ms = kLongPressThresholdMs,
            .dual_long_press_threshold_ms = kDualLongPressThresholdMs,
            .shutdown_prompt_threshold_ms = kShutdownPromptThresholdMs,
        },
        &::utils::CurrentSteadyMs);
    buttonActionRouter_ = std::make_unique<DA::Ugripper::ButtonActionRouter>(
        std::vector<DA::Ugripper::ButtonChordDefinition>{
            {
                .action = DA::Ugripper::ButtonAction::ToggleEgoBinding,
                .required_mask =
                    DA::Ugripper::ButtonMask(DA::Ugripper::PhysicalButton::LeftUp) |
                    DA::Ugripper::ButtonMask(DA::Ugripper::PhysicalButton::RightUp),
                .hold_ms = kEgoBindingChordHoldMs,
                .allowed_while_recording = false,
            },
        });

    std::vector<std::string> audioPlayerArguments = resolvePythonCommand();
    audioPlayerArguments.push_back(options_.audioPlayScript);
    audioCoordinator_ = std::make_unique<DA::Ugripper::AudioCoordinator>(
        &processSupervisor_,
        DA::Ugripper::AudioCoordinatorOptions{
            .player_arguments = std::move(audioPlayerArguments),
            .audio_pipe = options_.audioPipe,
            .audio_ready_file = options_.audioReadyFile,
            .player_stop_timeout_ms = 1000,
            .restart_interval_ms = kAudioPlayerRestartIntervalMs,
            .ready_grace_ms = kAudioPlayerReadyGraceMs,
            .startup_timeout_ms = 5000,
        },
        &::utils::CurrentSteadyMs);

    if (stereoEnabled_)
    {
        stereoSessionClient_ = std::make_unique<DA::Ugripper::StereoSessionClient>(
            &processSupervisor_,
            DA::Ugripper::StereoSessionClientOptions{
                .daemon_arguments =
                    {
                        options_.faysStereoDaemonScript,
                        "--control-fifo",
                        options_.stereoControlPipe,
                        "--status-file",
                        options_.stereoStatusFile,
                        "--left-config",
                        options_.leftFaysConfig,
                        "--right-config",
                        options_.rightFaysConfig,
                        "--left-fifo",
                        options_.leftFaysControlFifo,
                        "--right-fifo",
                        options_.rightFaysControlFifo,
                },
                .control_pipe = options_.stereoControlPipe,
                .left_control_fifo = options_.leftFaysControlFifo,
                .right_control_fifo = options_.rightFaysControlFifo,
                .status_file = options_.stereoStatusFile,
                .daemon_stop_timeout_ms = 2000,
                .restart_interval_ms = kStereoDaemonRestartIntervalMs,
                .finalize_wait_poll_ms = kStereoFinalizeWaitPollMs,
            },
            &::utils::CurrentSteadyMs);
    }
    shutdownRequestPort_ =
        DA::Ugripper::CreateFileShutdownRequestPort(
            options_.systemActionRequestFile,
            options_.systemActionResultFile);

    const auto criticalDevicePaths = activeCriticalDevicePaths(chestCameraEnabled_, stereoEnabled_);
    healthMonitor_ = std::make_unique<DA::Ugripper::HealthMonitor>(
        DA::Ugripper::HealthMonitorOptions{
            .disk_root = options_.diskRoot,
            .stereo_status_file = options_.stereoStatusFile,
            .stereo_enabled = stereoEnabled_,
            .critical_device_paths =
                std::vector<std::string>(criticalDevicePaths.begin(), criticalDevicePaths.end()),
            .poll_interval_ms = kHealthCheckIntervalMs,
            .hmi_active_timeout_ms = kHmiActiveTimeoutMs,
            .stereo_startup_grace_ms = kStereoStartupReadyTimeoutMs,
        },
        DA::Ugripper::HealthMonitor::Dependencies{
            .get_disk_fault =
                [this](const std::string& path) {
                    return detectDataDiskHealthFault(path);
                },
            .path_exists =
                [](const std::string& path) {
                    return fs::exists(path);
                },
            .get_process_status =
                [this](DA::Ugripper::WorkerName worker) {
                    return processSupervisor_.GetStatus(worker);
                },
            .get_hmi_health =
                [this](uint64_t active_timeout_ms) {
                    const auto health = panelManager_.getHealthSnapshot(active_timeout_ms);
                    return DA::Ugripper::HmiHealthSnapshot{
                        .has_connected_device = health.hasConnectedDevice,
                        .input_connected = health.inputConnected,
                        .input_active = health.inputActive,
                        .input_last_rx_age_ms = health.inputLastRxAgeMs,
                        .disconnected_ports = health.disconnectedPorts,
                        .inactive_ports = health.inactivePorts,
                        .inactive_port_details = health.inactivePortDetails,
                        .port_activity = health.portActivity,
                    };
                },
        },
        &::utils::CurrentSteadyMs);

    recordingOrchestrator_ = std::make_unique<DA::Ugripper::RecordingOrchestrator>(
        DA::Ugripper::RecordingOrchestratorOptions{
            .camera_recorder_bin = options_.cameraRecorderBin,
            .sensor_recorder_bin = options_.sensorRecorderBin,
            .camera_codec = options_.cameraCodec,
            .session_camera_streams_csv = joinCsv(sessionCameraStreams(chestCameraEnabled_)),
            .stereo_enabled = stereoEnabled_,
            .worker_stop_timeout_ms = 5000,
            .stereo_finalize_timeout_ms = 10000,
        },
        DA::Ugripper::RecordingOrchestrator::Dependencies
        {
            .episode = {
                .create_next_episode_dir =
                    [this]() {
                        return episodeManager_ != nullptr ? episodeManager_->CreateNextEpisodeDir() : std::string();
                    },
                .prepare_episode =
                    [this](const std::string& episode_dir,
                           bool reset_recording,
                           const std::string& reset_source_dir,
                           std::string* error_message) {
                        return episodeManager_ != nullptr &&
                               episodeManager_->PrepareEpisode(
                                   episode_dir, reset_recording, reset_source_dir, error_message);
                    },
                .validate_episode =
                    [this](const std::string& episode_dir,
                           std::string* error_message,
                           std::vector<std::string>* error_types) {
                        if (episodeManager_ == nullptr)
                        {
                            return false;
                        }
                        if (!episodeManager_->ValidateEpisode(episode_dir, error_message, error_types, nullptr))
                            return false;
                        if (stopTactileEpisodeDir_ != episode_dir) {
                            stopTactileFindings_.clear();
                            episodeManager_->ValidateTactileEpisode(episode_dir, &stopTactileFindings_, false);
                        }
                        if (!episodeManager_->PublishTactileValidation(episode_dir, stopTactileFindings_, error_message)) {
                            if (error_types) error_types->push_back("finalize_error");
                            return false;
                        }
                        flushEpisodeArtifactsToDisk(
                            fs::path(episode_dir), chestCameraEnabled_, stereoEnabled_, "metadata_final");
                        applyTactileValidationFindings(stopTactileFindings_);
                        stopTactileEpisodeDir_.clear();
                        stopTactileFindings_.clear();
                        return true;
                    },
                .prevalidate_local = [this](const std::string &episode_dir) {
                    stopTactileEpisodeDir_.clear();
                    stopTactileFindings_.clear();
                    if (episodeManager_ != nullptr) {
                        episodeManager_->PrevalidateLocalArtifacts(episode_dir);
                        episodeManager_->ValidateTactileEpisode(episode_dir, &stopTactileFindings_, false);
                        stopTactileEpisodeDir_ = episode_dir;
                    }
                },
                .attach_pending_pre_audio =
                    [this](const std::string& episode_dir) {
                        return attachPendingPreAudio(episode_dir);
                    },
                .merge_episode_info =
                    [this](const std::string& episode_dir, std::string* error_message) {
                        return mergeEpisodeInfo(episode_dir, error_message);
                    },
                .flush_episode_artifacts =
                    [this](const std::string& episode_dir, const char* stage) {
                        flushEpisodeArtifactsToDisk(
                            fs::path(episode_dir), chestCameraEnabled_, stereoEnabled_, stage);
                    },
                .write_episode_metadata =
                    [this](const std::string& episode_dir,
                           bool quality_ok,
                           const std::string& error_message,
                           const std::string& error_type) {
                        if (episodeManager_ == nullptr)
                        {
                            return false;
                        }
                        episodeManager_->SetTaskMarkers(
                            taskMarkerTracker_.GetTaskStartS(),
                            taskMarkerTracker_.GetTaskStopS());
                        std::string metadataError;
                        if (!episodeManager_->WriteFinalMetadata(
                                episode_dir,
                                quality_ok,
                                error_message,
                                error_type,
                                &metadataError))
                        {
                            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to write metadata.json: "
                                          << metadataError << std::endl).str());
                            return false;
                        }
                        flushEpisodeArtifactsToDisk(
                            fs::path(episode_dir), chestCameraEnabled_, stereoEnabled_, "metadata_final");
                        return true;
                    },
                .write_validation_error_log =
                    [](const std::string& episode_dir, const std::string& error_message) {
                        writeValidationErrorLog(fs::path(episode_dir), error_message);
                    },
                .finalize_episode_dir =
                    [this](const std::string& episode_dir, std::string* error_message) {
                        if (episodeManager_ == nullptr)
                        {
                            if (error_message != nullptr)
                            {
                                *error_message = "episode manager not initialized";
                            }
                            return std::string();
                        }
                        return episodeManager_->FinalizeEpisodeDir(episode_dir, error_message);
                    },
                .detect_recording_hardware_fault =
                    [this](const std::string& episode_dir) {
                        const auto fault = detectCameraRecorderStartupFailureForEpisode(episode_dir);
                        if (fault.has_value())
                        {
                            activeHardwareFault_ = *fault;
                        }
                        if (!fault.has_value()) {
                            const auto status = processSupervisor_.GetStatus(DA::Ugripper::WorkerName::SensorRecorder);
                            const auto type = status.running ? std::string_view{} :
                                ugripper::sensor::EncoderExitErrorType(status.last_exit_code);
                            if (!type.empty()) {
                                const bool left = status.last_exit_code == ugripper::sensor::kEncoderStaleLeftExit;
                                return std::optional<DA::Ugripper::HealthFault>{DA::Ugripper::HealthFault{
                                    DA::Ugripper::RuntimeLedState::Error2,
                                    left ? DA::Ugripper::HardwareFaultSide::Left : DA::Ugripper::HardwareFaultSide::Right,
                                    std::string(type), std::string(left ? "left" : "right") + " encoder failed during recording"}};
                            }
                        }
                        return fault;
                    },
            },
            .file_system = {
                .path_exists =
                    [](const std::string& path) {
                        return fs::exists(path);
                    },
                .write_recording_lock =
                    [this](const std::string& episode_dir) {
                        return writeRecordingLock(episode_dir);
                    },
                .remove_recording_lock =
                    [this]() {
                        removeRecordingLock();
                    },
            },
            .workers = {
                .prepare_sensor_start = nullptr,
                .finalize_sensor_start = nullptr,
                .start_worker =
                    [this](DA::Ugripper::WorkerName worker,
                           const DA::Ugripper::ProcessSpec& spec,
                           std::string* error_message) {
                        if (worker != DA::Ugripper::WorkerName::SensorRecorder)
                            return processSupervisor_.Start(worker, spec, error_message);
                        int readyPipe[2];
                        if (::pipe2(readyPipe, O_CLOEXEC) != 0) return false;
                        auto sensorSpec = spec;
                        sensorSpec.argv.insert(sensorSpec.argv.end(),
                                               {"--ready-fd", std::to_string(readyPipe[1])});
                        sensorSpec.inherited_fds.push_back(readyPipe[1]);
                        const bool started = processSupervisor_.Start(worker, sensorSpec, error_message);
                        ::close(readyPipe[1]);
                        const bool ready = started && ugripper::sensor::WaitEncoderReady(readyPipe[0]);
                        ::close(readyPipe[0]);
                        if (!ready) {
                            DM_LOG_ERROR_CODE("UGR-30-02", "Encoder startup failed: both sides must deliver samples within {}ms",
                                              ugripper::sensor::kEncoderReadyTimeoutMs);
                            if (started) processSupervisor_.Stop(worker, "encoder startup failed", nullptr);
                        }
                        return ready;
                    },
                .stop_worker =
                    [this](DA::Ugripper::WorkerName worker,
                           const std::string& reason,
                           std::string* error_message) {
                        return processSupervisor_.Stop(worker, reason, error_message);
                    },
                .get_worker_status =
                    [this](DA::Ugripper::WorkerName worker) {
                        return processSupervisor_.GetStatus(worker);
                    },
            },
            .stereo = {
                .start_session =
                    [this](const std::string& episode_dir, int64_t start_system_time_us, std::string* error_message) {
                        if (stereoSessionClient_ == nullptr)
                        {
                            if (error_message != nullptr)
                            {
                                *error_message = "stereo session client not initialized";
                            }
                            return false;
                        }
                        return stereoSessionClient_->StartSession(
                            episode_dir, start_system_time_us, error_message);
                    },
                .stop_session =
                    [this](const std::string& episode_dir, int64_t stop_system_time_us, std::string* error_message) {
                        if (stereoSessionClient_ == nullptr)
                        {
                            if (error_message != nullptr)
                            {
                                *error_message = "stereo session client not initialized";
                            }
                            return false;
                        }
                        return stereoSessionClient_->StopSession(
                            episode_dir, stop_system_time_us, error_message);
                    },
                .wait_for_finalize =
                    [this](const std::string& episode_dir, int timeout_ms, std::string* error_message) {
                        return waitForStereoFinalize(episode_dir, timeout_ms, error_message);
                    },
            },
            .ego = {
                .start_recording =
                    [this](const std::string& episode_dir,
                           int64_t start_system_time_us,
                           std::string* error_message,
                           std::string* error_type) {
                        return startEgoRecording(
                            episode_dir, start_system_time_us, error_message, error_type);
                    },
                .stop_recording =
                    [this](const std::string& episode_dir, int64_t stop_system_time_us, std::string* error_message) {
                        return stopEgoRecording(episode_dir, stop_system_time_us, error_message);
                    },
                .wait_for_finalize =
                    [this](const std::string& episode_dir,
                           int timeout_ms,
                           std::string* error_message,
                           std::string* error_type) {
                        return waitForEgoFinalize(episode_dir, timeout_ms, error_message, error_type);
                    },
                .cleanup_remote =
                    [this](const std::string& episode_dir, std::string* error_message) {
                        return cleanupEgoRemote(episode_dir, error_message);
                    },
                .is_required =
                    [this]() {
                        return isEgoRequired();
                    },
            },
            .feedback = {
                .set_audio_recovery_command =
                    [this](const std::string& command) {
                        setAudioRecoveryCommand(command);
                    },
                .send_audio_command =
                    [this](const std::string& command) {
                        sendAudioCommand(command);
                    },
                .set_led_state =
                    [this](DA::Ugripper::RuntimeLedState state, double progress) {
                        setLedState(toLedState(state), progress);
                    },
                .set_hardware_fault_led_state =
                    [this](const DA::Ugripper::HealthFault& fault) {
                        setHardwareFaultLedState(fault);
                    },
                .handle_failure_state =
                    [this](DA::Ugripper::RuntimeLedState state,
                           const std::vector<std::string>& error_types,
                           const std::string& detail) {
                        handleFailureState(state, error_types, detail);
                    },
            },
            .logs = {
                .log_info =
                    [](const std::string& message) {
                        DM_LOG_INFO("{}", (::DA::utils::LogString() << message << std::endl).str());
                    },
                .log_perf =
                    [this](const std::string& message) {
                        if (perfLogEnabled_)
                        {
                            DM_LOG_INFO("{}", (::DA::utils::LogString() << message << std::endl).str());
                        }
                    },
                .log_warn =
                    [](const std::string& message) {
                        DM_LOG_WARN("{}", (::DA::utils::LogString() << message << std::endl).str());
                    },
                .log_error =
                    [](const std::string& message) {
                        DM_LOG_ERROR_CODE("UGR-00-01", "{}", (::DA::utils::LogString() << message << std::endl).str());
                    },
                .log_typed_error =
                    [](const std::string& type, const std::string& message) {
                        DM_LOG_ERROR_CODE(DA::Ugripper::UgrCodeForErrorType(type), "{}", message);
                    },
                .snapshot_system_log = [this](const std::string &reason) {
                        const bool ok = snapshotSystemLog(reason);
                        if (!ok) systemLogEarlyFailed_.store(true);
                        return ok;
                    },
                .sync_runtime_log =
                    [this](const std::string& reason) {
                        return syncRuntimeLogToDisk(reason.c_str());
                    },
            },
        },
        &currentEpochUs,
        &steadyNowMs);

    pendingGripperRefresh_.fill(false);

    for (const std::string side : {"left", "right"})
    {
        refreshGripperRuntimeStateForSide(side, false);
    }
    if (episodeManager_ != nullptr)
    {
        episodeManager_->SetGripperRuntimeStates(gripperRuntimeStates_);
    }
    panelManager_.consumeConnectionEvents();

    startEgoFeedback();
    startAudioPlayer();
    healthState_.first_seen_ms = currentSteadyMs();
    startStereoDaemon();
    initializeRecordControlPipe();

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    applyIdleState();
    maintainDataStorage();
    if (!tactileWarningActive_ && episodeManagerInitialized_ && !currentHealthFault().has_value())
    {
        setAudioRecoveryCommand("ready");
        sendAudioCommand("ready");
    }

    initialized_ = true;
    return true;
}

int RecordRuntime::Run()
{
    if (!initialized_)
    {
        return 1;
    }

    DM_LOG_INFO("{}", (::DA::utils::LogString() << "record_runtime started" << std::endl).str());
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "device_sn=" << deviceSn_ << std::endl).str());
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "episode_root=" << episodeManager_->GetDataRoot() << std::endl).str());

    while (!stopRequested_.load(std::memory_order_relaxed))
    {
        const uint64_t loopStartMs = currentSteadyMs();
        maintainMainCameraRuntimeStates();
        maintainTactileCameraRuntimeStates();
        maintainAudioPlayer();
        maintainStereoDaemon();
        maintainDataStorage();
        maintainBackgroundTactileValidation();
        maintainEgoBindingAction();
        maintainEgoBindingHealth();
        maintainSystemLogWorker();
        maintainEgoWarmWorker();
        maintainEgoIncrementalSync();
        maintainRestoreUsbDevicePresence();
        maintainRestoreUsbRetry();
        maintainRestoreUsbFailureAlarm();
        monitorStereoFrameLoss();
        monitorHardwareHealth();
        maintainEgoSyncFeedback();

        ButtonSnapshot buttons;
        panelManager_.poll(options_.pollMs, &buttons);
        handleGripperConnectionEvents();
        processPendingGripperRefreshes();
        maintainGripperCalibration();
        maintainFirmwareStatus();
        const bool recordControlAccepted = pollRecordControlPipe();
        if (!recordControlAccepted)
        {
            ButtonSnapshot leftButtons;
            const bool hasLeftButtons =
                panelManager_.getButtonsForPortToken("left_gripper", &leftButtons);
            DA::Ugripper::ButtonRouteResult route;
            if (buttonActionRouter_ != nullptr && hasLeftButtons)
            {
                route = buttonActionRouter_->Update(
                    DA::Ugripper::FourButtonSnapshot{
                        .left_up = leftButtons.upPressed,
                        .left_down = leftButtons.downPressed,
                        .right_up = buttons.upPressed,
                        .right_down = buttons.downPressed,
                    },
                    isRecordingActive(),
                    currentSteadyMs());
            }
            if (route.consumed_mask != 0)
            {
                clearPendingPhysicalRecordShortPress("cross_hand_chord", currentSteadyMs());
                if (hmiController_ != nullptr)
                {
                    hmiController_->Reset();
                }
                resetLeftButtonTracking();
            }
            else
            {
                handleButtons(buttons);
                if (hasLeftButtons)
                {
                    handleLeftButtons(leftButtons);
                }
            }
            for (const auto action : route.actions)
            {
                handleButtonAction(action);
            }
        }

        if (isRecordingActive() && !checkRecorderProcesses())
        {
            const auto status = processSupervisor_.GetStatus(DA::Ugripper::WorkerName::SensorRecorder);
            const auto encoderError = status.running ? std::string_view{} :
                ugripper::sensor::EncoderExitErrorType(status.last_exit_code);
            if (!encoderError.empty()) {
                const std::string side = status.last_exit_code == ugripper::sensor::kEncoderStaleLeftExit ? "left" :
                    status.last_exit_code == ugripper::sensor::kEncoderStaleRightExit ? "right" : "left/right";
                DM_LOG_ERROR_CODE(DA::Ugripper::UgrCodeForErrorType(encoderError),
                                  "{} encoder fault; stopping recording", side);
                stopRecording(true, side + " encoder fault", std::string(encoderError));
            } else {
                stopRecording(true, "camera or sensor recorder exited unexpectedly", DA::Ugripper::kErrorTypeRuntimeError);
            }
        }

        const uint64_t loopElapsedMs = currentSteadyMs() - loopStartMs;
        if (loopElapsedMs < static_cast<uint64_t>(options_.pollMs))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.pollMs) - std::chrono::milliseconds(loopElapsedMs));
        }
    }

    stopEgoWarmWorker();
    stopRecording(false, "shutdown");
    stopSystemLogWorker();
    stopStereoDaemon();
    stopEgoSyncFeedback();
    setLedState(LedState::Exit);
    return 0;
}

void RecordRuntime::Stop() noexcept
{
    stopRequested_.store(true, std::memory_order_relaxed);
}

bool RecordRuntime::ensureEpisodeManagerInitialized()
{
    if (episodeManagerInitialized_)
    {
        return true;
    }
    if (episodeManager_ == nullptr)
    {
        DM_LOG_ERROR_CODE("UGR-00-01", "{}", (::DA::utils::LogString()
            << "episode manager is not constructed"
            << std::endl).str());
        return false;
    }

    if (!episodeManager_->Initialize())
    {
        if (!dataStorageWaitLogged_)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "waiting for writable data storage: episode manager initialization failed"
                << " disk_root=" << options_.diskRoot
                << std::endl).str());
            dataStorageWaitLogged_ = true;
        }
        setLedState(LedState::WaitStorage);
        return false;
    }

    episodeManagerInitialized_ = true;
    dataStorageWaitLogged_ = false;
    activeHardwareFault_.reset();
    episodeManager_->SetGripperRuntimeStates(gripperRuntimeStates_);
    syncMainCameraRuntimeStatesToEpisodeManager();
    syncTactileCameraRuntimeStatesToEpisodeManager();

    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "data storage ready"
        << " episode_root=" << episodeManager_->GetDataRoot()
        << std::endl).str());
    if (!isRecordingActive())
    {
        applyIdleState();
        if (!tactileWarningActive_ && !currentHealthFault().has_value())
        {
            setAudioRecoveryCommand("ready");
            sendAudioCommand("ready");
        }
    }
    return true;
}

std::optional<DA::Ugripper::HealthFault> RecordRuntime::detectDataDiskHealthFault(const std::string &path)
{
    return detectDiskHealthFault(path);
}

bool RecordRuntime::maintainDataStorage()
{
    if (episodeManagerInitialized_)
    {
        return true;
    }

    const auto diskFault = detectDataDiskHealthFault(options_.diskRoot);
    if (diskFault.has_value())
    {
        if (!dataStorageWaitLogged_)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "waiting for writable data storage"
                << " disk_root=" << options_.diskRoot
                << " detail=" << diskFault->detail
                << std::endl).str());
            dataStorageWaitLogged_ = true;
        }
        setLedState(LedState::WaitStorage);
        return false;
    }

    dataStorageWaitLogged_ = false;
    return ensureEpisodeManagerInitialized();
}

bool RecordRuntime::checkRecorderProcesses()
{
    return recordingOrchestrator_ != nullptr && recordingOrchestrator_->CheckRecorderProcesses();
}

void RecordRuntime::monitorStereoFrameLoss()
{
    if (!isRecordingActive()) {
        stereoLossEpisode_.clear();
        stereoLossStopDeadlineMs_ = 0;
        return;
    }
    const std::string episode = currentEpisodeDir();
    if (stereoLossEpisode_ != episode) {
        stereoLossEpisode_ = episode;
        stereoLossStopDeadlineMs_ = 0;
    }
    for (const char* camera : {"stereo_left.mkv", "stereo_right.mkv"}) {
        std::ifstream input(fs::path(episode) / (std::string(camera) + ".frame_loss.json"));
        if (!input) continue;
        const auto loss = json::parse(input, nullptr, false);
        if (loss.is_discarded() || !loss.is_object()) continue;
        const auto count = loss.value("max_consecutive_dropped_frames", uint64_t{0});
        if (count > 3 && stereoLossStopDeadlineMs_ == 0) {
            stereoLossStopDeadlineMs_ = currentSteadyMs() + 1000;
            DM_LOG_ERROR_CODE("UGR-22-04", "stereo frame loss camera={} max_consecutive_dropped_frames={} stop_delay_ms=1000", camera, count);
        }
    }
    if (stereoLossStopDeadlineMs_ && currentSteadyMs() >= stereoLossStopDeadlineMs_) {
        stopRecording(true, "stereo dropped more than 3 consecutive frames; retained 1s after detection", DA::Ugripper::kErrorTypeStereoFrameLoss);
    }
}

void RecordRuntime::monitorHardwareHealth()
{
    if (healthMonitor_ == nullptr)
    {
        return;
    }
    if (!episodeManagerInitialized_)
    {
        setLedState(LedState::WaitStorage);
        return;
    }

    const auto previousHealthState = healthState_;
    const auto result = healthMonitor_->Poll(healthState_);
    healthState_ = result.state;
    if (!result.checked)
    {
        return;
    }

    auto handleHardwareFault =
        [this](const DA::Ugripper::HealthFault &fault, bool shouldNotifyFault) {
            activeHardwareFault_ = fault;
            if (isRecordingActive())
            {
                const std::string stopReason =
                    "recording hardware fault (" + fault.key + "): " + fault.detail;
                if (shouldNotifyFault)
                {
                    DM_LOG_ERROR_CODE(DA::Ugripper::UgrCodeForErrorType(fault.key), "{}", (::DA::utils::LogString() << stopReason << std::endl).str());
                    DM_LOG_INFO("{}", (::DA::utils::LogString()
                        << "[HMI_DIAG] category=health_fault"
                        << " key=" << fault.key
                        << " led_state=" << ledStateName(toLedState(fault.led_state))
                        << " action=stop_recording").str());
                }
                const bool stopOk = stopRecording(true, stopReason, fault.key);
                const bool recordingStopped = !isRecordingActive();
                if (stopOk || recordingStopped || IsCameraRecorderMainCameraFault(fault))
                {
                    if (!stopOk && !recordingStopped)
                    {
                        DM_LOG_WARN("{}", (::DA::utils::LogString()
                            << "recording did not stop; continue restore usb for camera recorder main camera fault"
                            << " key=" << fault.key
                            << std::endl).str());
                    }
                    else if (!stopOk)
                    {
                        DM_LOG_WARN("{}", (::DA::utils::LogString()
                            << "recording stopped with failed episode; keep restore usb pending"
                            << " key=" << fault.key
                            << std::endl).str());
                    }
                    maybeTriggerRestoreUsbForHardwareFault(fault);
                }
                else
                {
                    DM_LOG_ERROR_CODE("UGR-00-06", "{}", (::DA::utils::LogString()
                        << "skip restore usb on recording hardware fault: recording still active after stop"
                        << " key=" << fault.key
                        << std::endl).str());
                }
                return;
            }
            maybeTriggerRestoreUsbForHardwareFault(fault);
            if (shouldNotifyFault)
            {
                DM_LOG_ERROR_CODE(DA::Ugripper::UgrCodeForErrorType(fault.key), "{}", (::DA::utils::LogString() << "hardware health fault (" << fault.key << "): "
                                      << fault.detail << std::endl).str());
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "[HMI_DIAG] category=health_fault"
                    << " key=" << fault.key
                    << " led_state=" << ledStateName(toLedState(fault.led_state))).str());
                setHardwareFaultLedState(fault);
                sendAudioCommand("error");
            }
        };

    if (result.fault.has_value())
    {
        handleHardwareFault(*result.fault, result.should_notify_fault);
        return;
    }

    if (const auto cameraStartupFault = detectCameraRecorderStartupFailure())
    {
        const std::string errorKey = cameraStartupFault->key + "|" + cameraStartupFault->detail;
        const bool shouldNotifyFault =
            healthState_.status != DA::Ugripper::HealthStatus::Error ||
            healthState_.last_error_key != errorKey;
        healthState_.status = DA::Ugripper::HealthStatus::Error;
        healthState_.last_error_key = errorKey;
        healthState_.last_check_ms = currentSteadyMs();
        handleHardwareFault(*cameraStartupFault, shouldNotifyFault);
        return;
    }

    if (const auto cameraHangFault = detectCameraRecorderKernelHang())
    {
        const std::string errorKey = cameraHangFault->key + "|" + cameraHangFault->detail;
        const bool shouldNotifyFault =
            healthState_.status != DA::Ugripper::HealthStatus::Error ||
            healthState_.last_error_key != errorKey;
        healthState_.status = DA::Ugripper::HealthStatus::Error;
        healthState_.last_error_key = errorKey;
        healthState_.last_check_ms = currentSteadyMs();
        handleHardwareFault(*cameraHangFault, shouldNotifyFault);
        return;
    }

    const bool becameReadyAfterStartup =
        previousHealthState.status == DA::Ugripper::HealthStatus::Unknown &&
        healthState_.status == DA::Ugripper::HealthStatus::Ok;
    if (result.recovered || becameReadyAfterStartup)
    {
        if (!restoreUsbPendingTriggerKey_.empty())
        {
            const uint64_t nowMs = currentSteadyMs();
            const uint64_t ageMs = restoreUsbPendingTriggerFirstSeenMs_ == 0
                                       ? 0
                                       : nowMs - restoreUsbPendingTriggerFirstSeenMs_;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "restore usb pending cleared"
                << " cause=" << restoreUsbPendingTriggerCause_
                << " evidence=" << restoreUsbPendingTriggerEvidence_
                << " side=" << restoreUsbPendingTriggerSide_
                << " age_ms=" << ageMs
                << std::endl).str());
            restoreUsbPendingTriggerKey_.clear();
            restoreUsbPendingTriggerCause_.clear();
            restoreUsbPendingTriggerEvidence_.clear();
            restoreUsbPendingTriggerSide_.clear();
            restoreUsbPendingTriggerFirstSeenMs_ = 0;
            restoreUsbPendingTriggerLastSeenMs_ = 0;
            restoreUsbPendingTriggerLogged_ = false;
        }
        activeHardwareFault_.reset();
        if (result.recovered)
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                               << "hardware health recovered"
                               << (previousHealthState.last_error_key.empty()
                                       ? std::string()
                                       : " from " + previousHealthState.last_error_key)
                               << std::endl)
                                  .str());
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "[HMI_DIAG] category=health_recovered"
                << " recording=" << boolText(isRecordingActive())).str());
        }
        else
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "[HMI_DIAG] category=health_ready"
                << " recording=" << boolText(isRecordingActive())).str());
        }
        if (isRecordingActive())
        {
            setLedState(LedState::Recording);
        }
        else
        {
            applyIdleState();
            if (!tactileWarningActive_)
            {
                sendAudioCommand("ready");
            }
        }
    }
}

bool RecordRuntime::isRecordingActive() const
{
    return recordingOrchestrator_ != nullptr && recordingOrchestrator_->GetState().is_recording;
}

const std::string &RecordRuntime::currentEpisodeDir() const
{
    static const std::string empty;
    if (recordingOrchestrator_ == nullptr)
    {
        return empty;
    }
    return recordingOrchestrator_->GetState().current_episode_dir;
}

const std::string &RecordRuntime::lastEpisodeDir() const
{
    static const std::string empty;
    if (recordingOrchestrator_ == nullptr)
    {
        return empty;
    }
    return recordingOrchestrator_->GetState().last_episode_dir;
}

void RecordRuntime::setLedState(LedState state, double progress)
{
    // Publish transitions in the existing recording lock even while finalize blocks the loop.
    if (maintenanceLockFd_ >= 0 && (state == LedState::Recording || state == LedState::Writing))
    {
        json lock;
        std::string ignored;
        if (loadJsonFile(options_.recordingLockFile, &lock, &ignored) && lock.is_object())
        {
            const std::string phase = state == LedState::Recording ? "recording" : "finalizing";
            if (lock.value("state", std::string()) != phase)
            {
                lock["state"] = phase;
                lock["updated_at_ms"] = currentEpochMs();
                writeTextFileAtomically(options_.recordingLockFile, lock.dump(2) + "\n", &ignored);
            }
        }
    }

    if (state == LedState::Ready && logPersistenceFailed_.load()) state = LedState::Error5;
    if (egoFeedback_) {
        const auto phase = state == LedState::Recording ? EgoSpeechPolicy::Phase::Recording
            : state == LedState::Writing ? EgoSpeechPolicy::Phase::Writing : EgoSpeechPolicy::Phase::Idle;
        egoFeedback_->SetPhase(phase);
    }
    const bool progressChanged =
        !hasLastLoggedLedState_ || std::fabs(lastLoggedLedProgress_ - progress) > 0.001;
    if (!hasLastLoggedLedState_ || lastLoggedLedState_ != state || progressChanged)
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "[HMI_DIAG] category=led_target"
            << " state=" << ledStateName(state)
            << " progress=" << std::fixed << std::setprecision(3) << progress).str());
        lastLoggedLedState_ = state;
        lastLoggedLedProgress_ = progress;
        hasLastLoggedLedState_ = true;
    }

    if (ledController_)
    {
        ledController_->setState(state, progress);
    }
}

void RecordRuntime::setTactileWarningLedState()
{
    if (!ledController_)
    {
        return;
    }

    const auto effectForSide = [](const TactileWarningSideState &state) -> GripperLedEffect {
        if (state.leftSensor && state.rightSensor)
        {
            return GripperLedEffect{GripperLedEffectState::TactileWarningBothSensors, 0.0};
        }
        if (state.leftSensor)
        {
            return GripperLedEffect{GripperLedEffectState::TactileWarningLeftSensor, 0.0};
        }
        if (state.rightSensor)
        {
            return GripperLedEffect{GripperLedEffectState::TactileWarningRightSensor, 0.0};
        }
        return GripperLedEffect{GripperLedEffectState::Ready, 0.0};
    };

    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "[HMI_DIAG] category=led_target"
        << " state=TactileWarning"
        << " left_l=" << boolText(tactileWarningSides_[0].leftSensor)
        << " left_r=" << boolText(tactileWarningSides_[0].rightSensor)
        << " right_l=" << boolText(tactileWarningSides_[1].leftSensor)
        << " right_r=" << boolText(tactileWarningSides_[1].rightSensor)).str());

    panelManager_.setLedEffectForSide("left", effectForSide(tactileWarningSides_[0]));
    panelManager_.setLedEffectForSide("right", effectForSide(tactileWarningSides_[1]));
}

void RecordRuntime::setHardwareFaultLedState(const DA::Ugripper::HealthFault &fault)
{
    const bool side_specific_error = fault.led_state == DA::Ugripper::RuntimeLedState::Error2 ||
                                     fault.led_state == DA::Ugripper::RuntimeLedState::Error4;
    if (!side_specific_error || !ledController_)
    {
        setLedState(toLedState(fault.led_state));
        return;
    }

    const GripperLedEffect faultEffect = makeLedEffect(toLedState(fault.led_state), 0.0);
    const GripperLedEffect unknownEffect{GripperLedEffectState::Error2Unknown, 0.0};
    switch (fault.side)
    {
    case DA::Ugripper::HardwareFaultSide::Left:
        panelManager_.setLedEffectForSide("left", faultEffect);
        panelManager_.setLedColorForSide("right", 255, 0, 0);
        break;
    case DA::Ugripper::HardwareFaultSide::Right:
        panelManager_.setLedColorForSide("left", 255, 0, 0);
        panelManager_.setLedEffectForSide("right", faultEffect);
        break;
    case DA::Ugripper::HardwareFaultSide::Both:
        panelManager_.setLedEffect(faultEffect);
        break;
    case DA::Ugripper::HardwareFaultSide::Unknown:
    default:
        panelManager_.setLedEffect(fault.led_state == DA::Ugripper::RuntimeLedState::Error2
                                       ? unknownEffect
                                       : faultEffect);
        break;
    }
}

void RecordRuntime::maybeTriggerRestoreUsbForHardwareFault(const DA::Ugripper::HealthFault &fault)
{
    if (IsDiskHealthFault(fault))
    {
        logRestoreUsbSkipOnce("disk_fault:" + fault.key,
                              "skip restore usb on data disk fault: " + fault.key);
        return;
    }

    if (fault.led_state != DA::Ugripper::RuntimeLedState::Error2 &&
        fault.led_state != DA::Ugripper::RuntimeLedState::Error4)
    {
        return;
    }

    const std::string reason = std::string("hardware_fault:") + fault.key;
    if (shouldDeferRestoreUsbForSide(fault.side, reason))
    {
        return;
    }
    RestoreUsbTriggerContext context;
    context.cause = fault.key;
    context.evidence = compactRestoreUsbEvidence(fault.detail);
    context.side = restoreUsbSideName(fault.side);
    if (!confirmRestoreUsbTrigger(context))
    {
        return;
    }
    triggerRestoreUsbOnError(reason, IsCameraRecorderMainCameraFault(fault));
}

std::string RecordRuntime::readEnvValue(const std::string &envFile, const std::string &key)
{
    return ::utils::ReadEnvValue(envFile, key);
}

std::string RecordRuntime::queryPackageVersion(const std::string &packageName)
{
    const std::string command = "dpkg-query -W -f='${Version}' " + packageName + " 2>/dev/null";
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
        return "unknown";
    }

    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        output += buffer;
    }
    pclose(pipe);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
    {
        output.pop_back();
    }

    if (output.empty())
    {
        return "unknown";
    }
    return output;
}

std::string RecordRuntime::toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string RecordRuntime::jsonEscape(const std::string &value)
{
    std::string result;
    result.reserve(value.size());
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}

uint64_t RecordRuntime::currentSteadyMs()
{
    return ::utils::CurrentSteadyMs();
}

uint64_t RecordRuntime::currentEpochMs()
{
    return ::utils::CurrentEpochMs();
}

int64_t RecordRuntime::currentEpochUs()
{
    return ::utils::CurrentEpochUs();
}

bool RecordRuntime::fileExistsAndNotEmpty(const std::string &path)
{
    std::error_code error;
    return fs::exists(path, error) &&
           fs::is_regular_file(path, error) &&
           fs::file_size(path, error) > 0;
}

bool RecordRuntime::runCommandSync(const std::vector<std::string> &arguments)
{
    if (arguments.empty())
    {
        return false;
    }

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments)
    {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const int childFdLimit = ChildFileDescriptorLimit();
    const pid_t pid = fork();
    if (pid < 0)
    {
        return false;
    }

    if (pid == 0)
    {
        CloseChildFileDescriptors(childFdLimit);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace DA::Ugripper
