#include "record_runtime.h"
#include "record_runtime/runtime_episode_internal.h"
#include "record_runtime/runtime_split_internal.h"
#include "main_camera_calibration_data.h"
#include "record_runtime/gripper_refresh_logic.h"
#include "utils/ugripper_logger.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace ugripper::runtime::record_runtime_detail;

void RecordRuntime::handleFailureState(ugripper::runtime::RuntimeLedState state,
                                       const std::vector<std::string> &errorTypes,
                                       const std::string &detail)
{
    bool checkMainCameraRecovery = false;
    if (!shouldRestoreUsbForFailure(state, errorTypes, detail, &checkMainCameraRecovery))
    {
        return;
    }
    const std::string reason = "failure_state:" + std::string(ledStateName(toLedState(state))) +
                               " detail=" + detail;
    const auto side = activeHardwareFault_.has_value()
                          ? activeHardwareFault_->side
                          : restoreUsbSideForText(detail);
    if (shouldDeferRestoreUsbForSide(side, reason))
    {
        return;
    }
    RestoreUsbTriggerContext context;
    context.cause = std::string("failure_") + ledStateName(toLedState(state));
    context.evidence = compactRestoreUsbEvidence(detail);
    context.side = restoreUsbSideName(side);
    if (isCriticalDeviceMissingEvidence(detail))
    {
        if (!confirmRestoreUsbTrigger(context))
        {
            return;
        }
    }
    else
    {
        logRestoreUsbRequested(context);
    }
    triggerRestoreUsbOnError(reason, checkMainCameraRecovery);
}

bool RecordRuntime::shouldRestoreUsbForFailure(ugripper::runtime::RuntimeLedState state,
                                               const std::vector<std::string> &errorTypes,
                                               const std::string &detail,
                                               bool *checkMainCameraRecovery) const
{
    if (checkMainCameraRecovery != nullptr)
    {
        *checkMainCameraRecovery = false;
    }

    if (state == ugripper::runtime::RuntimeLedState::Error2 ||
        state == ugripper::runtime::RuntimeLedState::Error4)
    {
        return true;
    }

    if (state == ugripper::runtime::RuntimeLedState::Error3 ||
        state == ugripper::runtime::RuntimeLedState::Error5)
    {
        return false;
    }

    if (state != ugripper::runtime::RuntimeLedState::Error1)
    {
        return false;
    }

    const std::string lowered = toLower(detail);
    const bool mainCameraText =
        lowered.find("main camera") != std::string::npos ||
        lowered.find("cam_left") != std::string::npos ||
        lowered.find("cam_right") != std::string::npos ||
        lowered.find("cam_chest") != std::string::npos ||
        lowered.find("/dev/cam_") != std::string::npos;
    if (!mainCameraText)
    {
        return false;
    }

    const bool relevantErrorType =
        std::find(errorTypes.begin(), errorTypes.end(), ugripper::runtime::kErrorTypeCalibrationError) != errorTypes.end() ||
        std::find(errorTypes.begin(), errorTypes.end(), ugripper::runtime::kErrorTypeMissingFile) != errorTypes.end() ||
        std::find(errorTypes.begin(), errorTypes.end(), ugripper::runtime::kErrorTypeFrameLoss) != errorTypes.end() ||
        std::find(errorTypes.begin(), errorTypes.end(), ugripper::runtime::kErrorTypeCollectionDurationTooShort) != errorTypes.end();
    if (!relevantErrorType)
    {
        return false;
    }

    if (checkMainCameraRecovery != nullptr)
    {
        *checkMainCameraRecovery = true;
    }
    return true;
}

void RecordRuntime::resetCameraRecorderDStateTracking()
{
    cameraRecorderDStatePid_ = -1;
    cameraRecorderDStateTaskId_.clear();
    cameraRecorderDStateFirstSeenMs_ = 0;
    cameraRecorderDStateLastLogMs_ = 0;
}

std::optional<ugripper::runtime::HealthFault> RecordRuntime::detectCameraRecorderStartupFailure()
{
    if (!isRecordingActive())
    {
        return std::nullopt;
    }

    const std::string episodeDir = currentEpisodeDir();
    if (episodeDir.empty())
    {
        return std::nullopt;
    }

    return detectCameraRecorderStartupFailureForEpisode(episodeDir);
}

std::optional<ugripper::runtime::HealthFault> RecordRuntime::detectCameraRecorderStartupFailureForEpisode(const std::string &episodeDir)
{
    if (episodeDir.empty())
    {
        return std::nullopt;
    }

    const fs::path faultPath = cameraRecorderFaultPath(episodeDir);
    std::error_code error;
    if (!fs::exists(faultPath, error) || error)
    {
        return std::nullopt;
    }

    std::ifstream input(faultPath);
    if (!input.is_open())
    {
        return std::nullopt;
    }

    json root;
    try
    {
        root = json::parse(input);
    }
    catch (const std::exception &ex)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "camera recorder fault file parse failed: path=" << faultPath
            << " error=" << ex.what()
            << std::endl).str());
        return std::nullopt;
    }

    const std::string faultType = root.value("fault_type", std::string());
    if (faultType != kMainCameraV4l2StartupFailureKey &&
        faultType != kMainCameraShortStreamKey)
    {
        return std::nullopt;
    }

    const std::string faultEpisodeDir = root.value("episode_dir", std::string());
    if (!faultEpisodeDir.empty() && faultEpisodeDir != episodeDir)
    {
        return std::nullopt;
    }

    const std::string cameraName = root.value("camera_name", std::string());
    const std::string device = root.value("device", std::string());
    const std::string faultError = root.value("error", std::string());
    if (cameraName.empty() && device.empty() && faultError.empty())
    {
        return std::nullopt;
    }

    const bool shortStreamFault = faultType == kMainCameraShortStreamKey;
    const char *faultKey = shortStreamFault
        ? kMainCameraShortStreamKey
        : kMainCameraV4l2StartupFailureKey;
    std::ostringstream detail;
    detail << (shortStreamFault
                   ? "CameraRecorder main camera short stream"
                   : "CameraRecorder main camera startup failure")
           << " camera=" << (cameraName.empty() ? "unknown" : cameraName)
           << " device=" << (device.empty() ? "unknown" : device)
           << " error=" << (faultError.empty() ? "unknown" : faultError);
    if (shortStreamFault)
    {
        detail << " frame_count=" << root.value("frame_count", static_cast<uint64_t>(0))
               << " span_us=" << root.value("span_us", static_cast<int64_t>(-1))
               << " session_us=" << root.value("session_us", static_cast<int64_t>(-1))
               << " phase=" << root.value("phase", std::string("unknown"));
    }
    detail << " fault_file=" << faultPath.string();

    const std::string sideText = cameraName + " " + device + " " + faultError;
    return ugripper::runtime::HealthFault{
        ugripper::runtime::RuntimeLedState::Error2,
        restoreUsbSideForText(sideText),
        faultKey,
        detail.str(),
    };
}

std::optional<ugripper::runtime::HealthFault> RecordRuntime::detectCameraRecorderKernelHang()
{
    if (!isRecordingActive())
    {
        resetCameraRecorderDStateTracking();
        return std::nullopt;
    }

    const auto status = processSupervisor_.GetStatus(ugripper::runtime::WorkerName::CameraRecorder);
    if (!status.running || status.pid <= 0)
    {
        resetCameraRecorderDStateTracking();
        return std::nullopt;
    }

    const auto dStateTask = FindFirstDStateTask(status.pid);
    if (!dStateTask.has_value())
    {
        if (cameraRecorderDStateFirstSeenMs_ != 0)
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "camera recorder D-state cleared"
                << " pid=" << cameraRecorderDStatePid_
                << " tid=" << cameraRecorderDStateTaskId_
                << std::endl).str());
        }
        resetCameraRecorderDStateTracking();
        return std::nullopt;
    }

    const uint64_t nowMs = currentSteadyMs();
    const bool newTask =
        cameraRecorderDStatePid_ != status.pid ||
        cameraRecorderDStateTaskId_ != dStateTask->taskId ||
        cameraRecorderDStateFirstSeenMs_ == 0;
    if (newTask)
    {
        cameraRecorderDStatePid_ = status.pid;
        cameraRecorderDStateTaskId_ = dStateTask->taskId;
        cameraRecorderDStateFirstSeenMs_ = nowMs;
        cameraRecorderDStateLastLogMs_ = 0;
    }

    const uint64_t stuckMs = nowMs >= cameraRecorderDStateFirstSeenMs_
                                 ? nowMs - cameraRecorderDStateFirstSeenMs_
                                 : 0;
    std::ostringstream detail;
    detail << "CameraRecorder task stuck in D state"
           << " pid=" << status.pid
           << " tid=" << dStateTask->taskId
           << " comm=" << dStateTask->comm
           << " wchan=" << (dStateTask->wchan.empty() ? "unknown" : dStateTask->wchan)
           << " stuck_ms=" << stuckMs;

    if (cameraRecorderDStateLastLogMs_ == 0 ||
        nowMs - cameraRecorderDStateLastLogMs_ >= kCameraRecorderDStateLogIntervalMs)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "camera recorder D-state observed"
            << " pid=" << status.pid
            << " tid=" << dStateTask->taskId
            << " comm=" << dStateTask->comm
            << " wchan=" << (dStateTask->wchan.empty() ? "unknown" : dStateTask->wchan)
            << " stuck_ms=" << stuckMs
            << " threshold_ms=" << kCameraRecorderDStateFaultMs
            << std::endl).str());
        cameraRecorderDStateLastLogMs_ = nowMs;
    }

    if (stuckMs < kCameraRecorderDStateFaultMs)
    {
        return std::nullopt;
    }

    return ugripper::runtime::HealthFault{
        ugripper::runtime::RuntimeLedState::Error2,
        ugripper::runtime::HardwareFaultSide::Unknown,
        kMainCameraV4l2KernelHangKey,
        detail.str(),
    };
}

bool RecordRuntime::mainCameraRestoreTargetsHealthy() const
{
    for (const auto &state : mainCameraRuntimeStates_)
    {
        if (!state.enabled)
        {
            continue;
        }
        std::string calibrationDetail;
        if (!state.present ||
            !isYuzhouMainCameraSn(state.serialNumber) ||
            !state.calibrationPayloadCached ||
            !validateYuzhouMainCameraCalibrationPayload(state.calibrationPayload, &calibrationDetail))
        {
            return false;
        }
    }
    return true;
}

bool RecordRuntime::restoreUsbTargetRecovered() const
{
    if (healthState_.status == ugripper::runtime::HealthStatus::Error)
    {
        return false;
    }
    if (restoreUsbCheckMainCameraRecovery_ && !mainCameraRestoreTargetsHealthy())
    {
        return false;
    }
    return true;
}

bool RecordRuntime::restoreUsbCriticalSymlinksPresent(std::string *detail) const
{
    std::vector<std::string> missing;
    const auto paths = activeCriticalDevicePaths(chestCameraEnabled_, stereoEnabled_);
    for (const char *path : paths)
    {
        std::error_code error;
        if (path == nullptr || *path == '\0')
        {
            continue;
        }
        if (!fs::exists(path, error))
        {
            missing.push_back(path);
        }
    }

    if (detail != nullptr)
    {
        if (missing.empty())
        {
            detail->clear();
        }
        else
        {
            std::ostringstream stream;
            for (size_t index = 0; index < missing.size(); ++index)
            {
                if (index > 0)
                {
                    stream << ", ";
                }
                stream << missing[index];
            }
            *detail = stream.str();
        }
    }
    return missing.empty();
}

bool RecordRuntime::sideHasAnyRestoreUsbDevice(const std::string &side) const
{
    const char *gripperPath = side == "left" ? "/dev/left_gripper" : "/dev/right_gripper";
    std::error_code error;
    if (fs::exists(gripperPath, error))
    {
        return true;
    }

    const auto &paths = side == "left" ? kLeftCriticalDevicePaths : kRightCriticalDevicePaths;
    for (const char *path : paths)
    {
        if (!stereoEnabled_ && isStereoDevicePath(path))
        {
            continue;
        }
        error.clear();
        if (path != nullptr && *path != '\0' && fs::exists(path, error))
        {
            return true;
        }
    }
    return false;
}

bool RecordRuntime::shouldDeferRestoreUsbForSide(ugripper::runtime::HardwareFaultSide side,
                                                 const std::string &reason) const
{
    const uint64_t nowMs = currentSteadyMs();
    const auto sideDeferred = [&](const std::string &sideName) {
        const size_t index = gripperStateIndexForSide(sideName);
        if (!sideHasAnyRestoreUsbDevice(sideName))
        {
            return true;
        }
        const uint64_t graceUntilMs = restoreUsbInsertGraceUntilMs_[index];
        if (graceUntilMs != 0 && nowMs < graceUntilMs)
        {
            return true;
        }
        return false;
    };

    switch (side)
    {
    case ugripper::runtime::HardwareFaultSide::Left:
        return sideDeferred("left");
    case ugripper::runtime::HardwareFaultSide::Right:
        return sideDeferred("right");
    case ugripper::runtime::HardwareFaultSide::Both:
    {
        const bool leftDeferred = sideDeferred("left");
        const bool rightDeferred = sideDeferred("right");
        return leftDeferred || rightDeferred;
    }
    case ugripper::runtime::HardwareFaultSide::Unknown:
    default:
    {
        const bool leftDeferred = sideDeferred("left");
        const bool rightDeferred = sideDeferred("right");
        return leftDeferred || rightDeferred;
    }
    }
}

void RecordRuntime::triggerRestoreUsbFailureAlarm(const std::string &reason)
{
    if (restoreUsbFailureAlarmActive_)
    {
        return;
    }
    restoreUsbFailureAlarmActive_ = true;
    restoreUsbFailureAlarmStartMs_ = currentSteadyMs();

    bool wroteAny = false;
    wroteAny = panelManager_.setBeepEnabledForSide("left", true) || wroteAny;
    wroteAny = panelManager_.setBeepEnabledForSide("right", true) || wroteAny;

    DM_LOG_ERROR_CODE("UGR-80-05", "{}", (::DA::utils::LogString()
        << "restore usb failed after max attempts; hmi beep alarm triggered"
        << " attempts=" << restoreUsbAttemptsInCurrentError_
        << " side=both"
        << " wrote_any=" << boolText(wroteAny)
        << " reason=" << reason
        << std::endl).str());
}

void RecordRuntime::maintainRestoreUsbFailureAlarm()
{
    if (!restoreUsbFailureAlarmActive_ || restoreUsbFailureAlarmStartMs_ == 0)
    {
        return;
    }
    const uint64_t nowMs = currentSteadyMs();
    if (nowMs < restoreUsbFailureAlarmStartMs_ ||
        nowMs - restoreUsbFailureAlarmStartMs_ < kRestoreUsbFailureAlarmDurationMs)
    {
        return;
    }
    panelManager_.silenceBeep();
    restoreUsbFailureAlarmActive_ = false;
    restoreUsbFailureAlarmStartMs_ = 0;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "restore usb failure hmi beep alarm auto silenced"
        << " duration_ms=" << kRestoreUsbFailureAlarmDurationMs
        << std::endl).str());
}

void RecordRuntime::noteRestoreUsbManualInsert(const std::string &side, const std::string &reason)
{
    if (side != "left" && side != "right")
    {
        return;
    }
    if (restoreUsbInProgress_->load() || restoreUsbPendingRetryCheck_)
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "ignore restore usb manual insert during software restore settle"
            << " side=" << side
            << " reason=" << reason
            << std::endl).str());
        return;
    }
    const size_t index = gripperStateIndexForSide(side);
    const uint64_t nowMs = currentSteadyMs();
    resetRestoreUsbErrorWindow();
    restoreUsbSidePresent_[index] = true;
    restoreUsbInsertGraceUntilMs_[index] = nowMs + kRestoreUsbManualInsertGraceMs;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "restore usb manual insert detected; reset attempts and start settle grace"
        << " side=" << side
        << " grace_ms=" << kRestoreUsbManualInsertGraceMs
        << " reason=" << reason
        << std::endl).str());
}

void RecordRuntime::maintainRestoreUsbDevicePresence()
{
    if (restoreUsbInProgress_->load() || restoreUsbPendingRetryCheck_)
    {
        return;
    }

    for (const std::string side : {"left", "right"})
    {
        const size_t index = gripperStateIndexForSide(side);
        const bool present = sideHasAnyRestoreUsbDevice(side);
        if (present && !restoreUsbSidePresent_[index])
        {
            noteRestoreUsbManualInsert(side, "device_node_inserted");
            continue;
        }
        if (!present && restoreUsbSidePresent_[index])
        {
            resetRestoreUsbErrorWindow();
            restoreUsbSidePresent_[index] = false;
            restoreUsbInsertGraceUntilMs_[index] = 0;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "restore usb side appears unplugged"
                << " side=" << side
                << std::endl).str());
            continue;
        }
        if (present)
        {
            const uint64_t nowMs = currentSteadyMs();
            if (restoreUsbInsertGraceUntilMs_[index] != 0 && nowMs >= restoreUsbInsertGraceUntilMs_[index])
            {
                restoreUsbInsertGraceUntilMs_[index] = 0;
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "restore usb manual insert settle grace expired"
                    << " side=" << side
                    << std::endl).str());
            }
        }
    }
}

void RecordRuntime::resetRestoreUsbErrorWindow()
{
    restoreUsbAttemptsInCurrentError_ = 0;
    restoreUsbPendingRetryCheck_ = false;
    restoreUsbSymlinkCheckPassed_ = false;
    restoreUsbSymlinkPassedMs_ = 0;
    restoreUsbLastSymlinkProbeMs_ = 0;
    restoreUsbLastMissingLogMs_ = 0;
    restoreUsbLastMissingDetail_.clear();
    restoreUsbCheckMainCameraRecovery_ = false;
    restoreUsbLastReason_.clear();
    restoreUsbLastFinishMs_->store(0);
    restoreUsbPendingTriggerKey_.clear();
    restoreUsbPendingTriggerCause_.clear();
    restoreUsbPendingTriggerEvidence_.clear();
    restoreUsbPendingTriggerSide_.clear();
    restoreUsbPendingTriggerFirstSeenMs_ = 0;
    restoreUsbPendingTriggerLastSeenMs_ = 0;
    restoreUsbPendingTriggerLogged_ = false;
    restoreUsbLastSkipLogKey_.clear();
    if (restoreUsbFailureAlarmActive_)
    {
        panelManager_.silenceBeep();
        restoreUsbFailureAlarmActive_ = false;
    }
    restoreUsbFailureAlarmStartMs_ = 0;
    restoreUsbPreflightFailureUntilMs_ = 0;
    if (!restoreUsbInProgress_->load())
    {
        resumeStereoDaemonAfterRestoreUsb("restore_error_window_reset");
    }
}

bool RecordRuntime::isDataDiskMounted() const
{
    const fs::path diskRoot(options_.diskRoot);
    std::ifstream mountInfo("/proc/self/mountinfo");
    if (!mountInfo.is_open())
    {
        return false;
    }

    std::string line;
    while (std::getline(mountInfo, line))
    {
        const size_t separator = line.find(" - ");
        const std::string leftPart = separator == std::string::npos ? line : line.substr(0, separator);
        std::istringstream fields(leftPart);
        std::string mountId;
        std::string parentId;
        std::string majorMinor;
        std::string root;
        std::string mountPoint;
        if (!(fields >> mountId >> parentId >> majorMinor >> root >> mountPoint))
        {
            continue;
        }
        if (mountPoint == diskRoot.string())
        {
            return true;
        }
    }
    return false;
}

bool RecordRuntime::requestDataDiskUmountForRestoreUsb(const std::string &reason)
{
    if (shutdownRequestPort_ == nullptr)
    {
        DM_LOG_ERROR_CODE("UGR-80-02", "{}", (::DA::utils::LogString()
            << "skip restore usb: cannot umount data disk, system action port not initialized"
            << " reason=" << reason
            << std::endl).str());
        return false;
    }

    std::string errorMessage;
    DM_LOG_WARN("{}", (::DA::utils::LogString()
        << "restore usb preflight request data disk umount"
        << " disk_root=" << options_.diskRoot
        << " reason=" << reason
        << std::endl).str());
    if (!shutdownRequestPort_->RequestAction("umount", &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString()
            << "skip restore usb: failed to request data disk umount"
            << " error=" << errorMessage
            << " reason=" << reason
            << std::endl).str());
        return false;
    }

    std::string result;
    if (!shutdownRequestPort_->WaitForActionResult(
            &result,
            static_cast<int>(kSystemActionResultWaitMs),
            &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString()
            << "skip restore usb: timed out waiting for data disk umount"
            << " error=" << errorMessage
            << " reason=" << reason
            << std::endl).str());
        return false;
    }

    if (result != "ok")
    {
        DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString()
            << "skip restore usb: data disk umount failed"
            << " result=" << result
            << " reason=" << reason
            << std::endl).str());
        return false;
    }

    DM_LOG_WARN("{}", (::DA::utils::LogString()
        << "restore usb preflight data disk umounted"
        << " disk_root=" << options_.diskRoot
        << " reason=" << reason
        << std::endl).str());
    return true;
}

bool RecordRuntime::prepareDataDiskForRestoreUsb(const std::string &reason)
{
    const bool allowKernelHangRestore = IsCameraRecorderKernelHangReason(reason);
    if (isRecordingActive())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "restore usb preflight stop recording before power reset"
            << " reason=" << reason
            << std::endl).str());
        const std::string stopReason = "restore usb preflight before power reset: " + reason;
        if (!stopRecording(true, stopReason, ugripper::runtime::kErrorTypeRuntimeError))
        {
            if (!allowKernelHangRestore)
            {
                DM_LOG_ERROR_CODE("UGR-00-06", "{}", (::DA::utils::LogString()
                    << "skip restore usb: failed to stop recording before power reset"
                    << " reason=" << reason
                    << std::endl).str());
                return false;
            }
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "continue restore usb despite stop failure for camera recorder kernel hang"
                << " reason=" << reason
                << std::endl).str());
        }
    }

    if (!isDataDiskMounted())
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "restore usb preflight skip data disk umount: disk is not mounted"
            << " disk_root=" << options_.diskRoot
            << " reason=" << reason
            << std::endl).str());
        return true;
    }

    syncRuntimeLogToDisk("restore usb preflight");
    std::error_code flushError;
    if (!flushDirectoryToDisk(fs::path(options_.diskRoot), &flushError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "restore usb preflight failed to flush data disk root before umount"
            << " disk_root=" << options_.diskRoot
            << " error=" << flushError.message()
            << " reason=" << reason
            << std::endl).str());
    }

    if (!requestDataDiskUmountForRestoreUsb(reason))
    {
        if (!allowKernelHangRestore)
        {
            return false;
        }
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "continue restore usb despite data disk umount failure for camera recorder kernel hang"
            << " disk_root=" << options_.diskRoot
            << " reason=" << reason
            << std::endl).str());
    }
    return true;
}

void RecordRuntime::logRestoreUsbRequested(const RestoreUsbTriggerContext &context)
{
    DM_LOG_WARN("{}", (::DA::utils::LogString()
        << "restore usb requested"
        << " cause=" << context.cause
        << " evidence=" << context.evidence
        << " side=" << context.side
        << std::endl).str());
}

bool RecordRuntime::confirmRestoreUsbTrigger(const RestoreUsbTriggerContext &context)
{
    const uint64_t nowMs = currentSteadyMs();
    if (IsImmediateRestoreUsbTriggerCause(context.cause))
    {
        if (!restoreUsbPendingTriggerKey_.empty())
        {
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
        }
        restoreUsbPendingTriggerKey_.clear();
        restoreUsbPendingTriggerCause_.clear();
        restoreUsbPendingTriggerEvidence_.clear();
        restoreUsbPendingTriggerSide_.clear();
        restoreUsbPendingTriggerFirstSeenMs_ = 0;
        restoreUsbPendingTriggerLastSeenMs_ = 0;
        restoreUsbPendingTriggerLogged_ = false;
        return true;
    }

    const std::string key = context.cause + "|" + context.side + "|" + context.evidence;
    if (restoreUsbPendingTriggerKey_ != key)
    {
        if (!restoreUsbPendingTriggerKey_.empty())
        {
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
        }
        restoreUsbPendingTriggerKey_ = key;
        restoreUsbPendingTriggerCause_ = context.cause;
        restoreUsbPendingTriggerEvidence_ = context.evidence;
        restoreUsbPendingTriggerSide_ = context.side;
        restoreUsbPendingTriggerFirstSeenMs_ = nowMs;
        restoreUsbPendingTriggerLastSeenMs_ = nowMs;
        restoreUsbPendingTriggerLogged_ = false;
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "restore usb pending wait"
            << " cause=" << context.cause
            << " evidence=" << context.evidence
            << " side=" << context.side
            << " stable_ms=" << kRestoreUsbTriggerStableMs
            << std::endl).str());
        return false;
    }

    restoreUsbPendingTriggerLastSeenMs_ = nowMs;
    const uint64_t ageMs = restoreUsbPendingTriggerFirstSeenMs_ == 0
                               ? 0
                               : nowMs - restoreUsbPendingTriggerFirstSeenMs_;
    if (ageMs < kRestoreUsbTriggerStableMs)
    {
        return false;
    }

    if (!restoreUsbPendingTriggerLogged_)
    {
        logRestoreUsbRequested(context);
        restoreUsbPendingTriggerLogged_ = true;
    }
    return true;
}

void RecordRuntime::maintainRestoreUsbRetry()
{
    if (!restoreUsbPendingRetryCheck_ || restoreUsbInProgress_->load())
    {
        return;
    }

    const uint64_t finishMs = restoreUsbLastFinishMs_->load();
    if (finishMs == 0)
    {
        return;
    }

    const uint64_t nowMs = currentSteadyMs();
    if (nowMs < finishMs)
    {
        return;
    }

    const uint64_t elapsedMs = nowMs - finishMs;
    if (restoreUsbTargetRecovered())
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "restore usb recovered"
            << " attempt="
            << restoreUsbAttemptsInCurrentError_
            << " elapsed_ms=" << elapsedMs
            << std::endl).str());
        resetRestoreUsbErrorWindow();
        return;
    }

    if (!restoreUsbSymlinkCheckPassed_)
    {
        if (restoreUsbLastSymlinkProbeMs_ != 0 &&
            nowMs >= restoreUsbLastSymlinkProbeMs_ &&
            nowMs - restoreUsbLastSymlinkProbeMs_ < kRestoreUsbSymlinkPollIntervalMs)
        {
            return;
        }
        restoreUsbLastSymlinkProbeMs_ = nowMs;

        std::string missingDetail;
        if (!restoreUsbCriticalSymlinksPresent(&missingDetail))
        {
            if (elapsedMs < kRestoreUsbSymlinkMaxWaitMs)
            {
                if (restoreUsbLastMissingDetail_ != missingDetail ||
                    restoreUsbLastMissingLogMs_ == 0 ||
                    nowMs - restoreUsbLastMissingLogMs_ >= 5000)
                {
                    restoreUsbLastMissingDetail_ = missingDetail;
                    restoreUsbLastMissingLogMs_ = nowMs;
                    DM_LOG_INFO("{}", (::DA::utils::LogString()
                        << "restore usb symlinks waiting"
                        << " attempt=" << restoreUsbAttemptsInCurrentError_
                        << " elapsed_ms=" << elapsedMs
                        << " evidence=" << missingDetail
                        << std::endl).str());
                }
                return;
            }
            DM_LOG_ERROR_CODE("UGR-80-05", "{}", (::DA::utils::LogString()
                << "restore usb symlinks timeout"
                << " attempt=" << restoreUsbAttemptsInCurrentError_
                << " elapsed_ms=" << elapsedMs
                << " evidence=" << missingDetail
                << std::endl).str());
            restoreUsbPendingRetryCheck_ = false;
            if (restoreUsbAttemptsInCurrentError_ >= kRestoreUsbMaxAttemptsPerError)
            {
                resumeStereoDaemonAfterRestoreUsb(restoreUsbLastReason_ + ":critical_symlinks_missing");
                triggerRestoreUsbFailureAlarm(
                    restoreUsbLastReason_ + ":critical_symlinks_missing:" + missingDetail);
                return;
            }
            triggerRestoreUsbOnError(restoreUsbLastReason_ + ":critical_symlinks_missing_retry",
                                     restoreUsbCheckMainCameraRecovery_);
            return;
        }

        restoreUsbSymlinkCheckPassed_ = true;
        resumeStereoDaemonAfterRestoreUsb(restoreUsbLastReason_ + ":critical_symlinks_present");
        restoreUsbSymlinkPassedMs_ = currentSteadyMs();
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "restore usb symlinks ready"
            << " attempt="
            << restoreUsbAttemptsInCurrentError_
            << " elapsed_ms=" << elapsedMs
            << std::endl).str());
    }

    const uint64_t symlinkPassedMs = restoreUsbSymlinkPassedMs_ != 0 ? restoreUsbSymlinkPassedMs_ : finishMs;
    const uint64_t readyElapsedMs = nowMs >= symlinkPassedMs ? nowMs - symlinkPassedMs : 0;
    if (readyElapsedMs < kRestoreUsbReadyCheckDelayMs)
    {
        return;
    }

    restoreUsbPendingRetryCheck_ = false;
    if (restoreUsbTargetRecovered())
    {
        const int attempt = restoreUsbAttemptsInCurrentError_;
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "restore usb recovered"
            << " attempt="
            << attempt
            << " elapsed_ms=" << elapsedMs
            << " ready_elapsed_ms=" << readyElapsedMs
            << std::endl).str());
        resetRestoreUsbErrorWindow();
        return;
    }

    DM_LOG_ERROR_CODE("UGR-80-05", "{}", (::DA::utils::LogString()
        << "restore usb ready timeout"
        << " attempt=" << restoreUsbAttemptsInCurrentError_
        << " elapsed_ms=" << elapsedMs
        << " ready_elapsed_ms=" << readyElapsedMs
        << std::endl).str());
    if (restoreUsbAttemptsInCurrentError_ >= kRestoreUsbMaxAttemptsPerError)
    {
        triggerRestoreUsbFailureAlarm(restoreUsbLastReason_ + ":ready_timeout");
        return;
    }

    triggerRestoreUsbOnError(restoreUsbLastReason_ + ":ready_retry", restoreUsbCheckMainCameraRecovery_);
}

void RecordRuntime::logRestoreUsbSkipOnce(const std::string &key, const std::string &message)
{
    if (key.empty() || restoreUsbLastSkipLogKey_ == key)
    {
        return;
    }
    restoreUsbLastSkipLogKey_ = key;
    DM_LOG_WARN("{}", (::DA::utils::LogString() << message << std::endl).str());
}

void RecordRuntime::triggerRestoreUsbOnError(const std::string &reason, bool checkMainCameraRecovery)
{
    if (restoreUsbPendingRetryCheck_)
    {
        return;
    }

    if (restoreUsbAttemptsInCurrentError_ >= kRestoreUsbMaxAttemptsPerError)
    {
        return;
    }

    if (options_.restoreUsbCommand.empty())
    {
        logRestoreUsbSkipOnce("restore_command_empty",
                              "skip restore usb on error: restore command is empty");
        return;
    }

    std::error_code error;
    if (!fs::exists(options_.restoreUsbCommand, error))
    {
        logRestoreUsbSkipOnce(
            "restore_command_not_found:" + options_.restoreUsbCommand,
            (::DA::utils::LogString()
                << "skip restore usb on error: command not found: "
                << options_.restoreUsbCommand).str());
        return;
    }

    restoreUsbLastReason_ = reason;
    restoreUsbCheckMainCameraRecovery_ = restoreUsbCheckMainCameraRecovery_ || checkMainCameraRecovery;

    const uint64_t nowMs = currentSteadyMs();
    if (restoreUsbPreflightFailureUntilMs_ != 0 && nowMs < restoreUsbPreflightFailureUntilMs_)
    {
        return;
    }

    if (!prepareDataDiskForRestoreUsb(reason))
    {
        restoreUsbPreflightFailureUntilMs_ = currentSteadyMs() + kRestoreUsbPreflightFailureCooldownMs;
        DM_LOG_ERROR_CODE("UGR-80-02", "{}", (::DA::utils::LogString()
            << "skip restore usb on error: data disk preflight failed"
            << " cooldown_ms=" << kRestoreUsbPreflightFailureCooldownMs
            << " attempts=" << restoreUsbAttemptsInCurrentError_
            << " reason=" << reason
            << std::endl).str());
        return;
    }
    restoreUsbPreflightFailureUntilMs_ = 0;

    bool expected = false;
    if (!restoreUsbInProgress_->compare_exchange_strong(expected, true))
    {
        return;
    }

    restoreUsbLastSkipLogKey_.clear();
    suspendStereoDaemonForRestoreUsb(reason);

    ++restoreUsbAttemptsInCurrentError_;
    restoreUsbPendingRetryCheck_ = true;
    restoreUsbSymlinkCheckPassed_ = false;
    restoreUsbSymlinkPassedMs_ = 0;
    restoreUsbLastSymlinkProbeMs_ = 0;
    restoreUsbLastMissingLogMs_ = 0;
    restoreUsbLastMissingDetail_.clear();
    restoreUsbLastFinishMs_->store(0);

    const std::string command = "sudo -n " + shellQuote(options_.restoreUsbCommand);
    const auto inProgress = restoreUsbInProgress_;
    const auto lastFinishMs = restoreUsbLastFinishMs_;
    const int attempt = restoreUsbAttemptsInCurrentError_;
    std::thread([command, inProgress, lastFinishMs, attempt]() {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "restore usb start"
            << " attempt=" << attempt
            << std::endl).str());

        const int rc = std::system(command.c_str());
        if (rc == -1)
        {
            DM_LOG_ERROR_CODE("UGR-80-03", "{}", (::DA::utils::LogString()
                << "restore usb command failed to start"
                << " command=" << command
                << std::endl).str());
        }
        else if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0)
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "restore usb command completed"
                << std::endl).str());
        }
        else
        {
            DM_LOG_ERROR_CODE("UGR-80-03", "{}", (::DA::utils::LogString()
                << "restore usb command failed"
                << " command=" << command
                << " status=" << rc
                << std::endl).str());
        }
        lastFinishMs->store(RecordRuntime::currentSteadyMs());
        inProgress->store(false);
    }).detach();
}
