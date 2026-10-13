#include "record_runtime.h"
#include "record_runtime/runtime_episode_internal.h"
#include "record_runtime/runtime_split_internal.h"
#include "record_runtime/runtime_support_internal.h"
#include "record_runtime/error_code_mapping.h"
#include "record_runtime/episode_metadata_contract.h"
#include "utils/ugripper_logger.h"
#include "utils/time_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;
using namespace DA::Ugripper::detail;

namespace DA::Ugripper {

void RecordRuntime::sendAudioCommand(const std::string &command) const
{
    if (egoFeedback_ && command == "validation_failed") egoFeedback_->Notify(1204);
    if (audioCoordinator_ == nullptr)
    {
        return;
    }
    audioCoordinator_->SendCommand(command);
}

bool RecordRuntime::attachPendingPreAudio(const std::string &episodeDir)
{
    if (pendingPreAudioFile_.empty() || !fs::exists(pendingPreAudioFile_))
    {
        return true;
    }

    std::error_code error;
    const fs::path target = fs::path(episodeDir) / "audio_pre.wav";
    if (!moveFileWithCrossDeviceFallback(pendingPreAudioFile_, target, &error))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to move pre audio into episode: " << error.message() << std::endl).str());
        return false;
    }

    if (!flushFileToDisk(target, &error))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush pre audio into episode: " << error.message() << std::endl).str());
    }

    pendingPreAudioFile_.clear();
    return true;
}
bool RecordRuntime::writeRecordingLock(const std::string &episodeDir)
{
    if (maintenanceLockFd_ < 0)
    {
        maintenanceLockFd_ = open("/run/ugripper/maintenance.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0660);
        if (maintenanceLockFd_ < 0 || flock(maintenanceLockFd_, LOCK_EX | LOCK_NB) != 0)
        {
            if (maintenanceLockFd_ >= 0) close(maintenanceLockFd_);
            maintenanceLockFd_ = -1;
            DM_LOG_WARN("recording rejected: maintenance or download in progress");
            return false;
        }
    }
    json lock = {
        {"pid", static_cast<int>(getpid())},
        {"episode_dir", episodeDir},
        {"started_at_ms", currentEpochMs()},
    };
    std::string errorMessage;
    if (!writeTextFileAtomically(options_.recordingLockFile, lock.dump(2) + "\n", &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-10-02", "{}", (::DA::utils::LogString() << "failed to write recording lock "
                                                   << options_.recordingLockFile << ": "
                                                   << errorMessage << std::endl).str());
        removeRecordingLock();
        return false;
    }
    return true;
}
void RecordRuntime::removeRecordingLock()
{
    std::error_code error;
    fs::remove(options_.recordingLockFile, error);
    if (error)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to remove recording lock "
                                                  << options_.recordingLockFile << ": "
                                                  << error.message() << std::endl).str());
    }
    if (maintenanceLockFd_ >= 0)
    {
        close(maintenanceLockFd_);
        maintenanceLockFd_ = -1;
    }
}

bool RecordRuntime::startRecording(bool resetRecording)
{
    // Reject before creating an episode or recording lock. Do not queue a start.
    if (egoBindingActionFuture_.valid() || (isEgoRequired() && !isEgoSyncReady()))
    {
        egoSyncReady_ = false;
        DM_LOG_WARN("recording rejected: ego binding/time synchronization pending");
        maintainEgoSyncFeedback();
        applyIdleState();
        return false;
    }
    // Fast user-facing rejection. writeRecordingLock takes the authoritative lock
    // atomically before any recorder or episode is created.
    const int guard = open("/run/ugripper/maintenance.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0660);
    if (guard < 0 || flock(guard, LOCK_EX | LOCK_NB) != 0)
    {
        if (guard >= 0) close(guard);
        DM_LOG_WARN("recording rejected: maintenance or download in progress");
        return false;
    }
    close(guard);
    if (recordingOrchestrator_ == nullptr)
    {
        DM_LOG_ERROR_CODE("UGR-00-01", "{}", (::DA::utils::LogString() << "recording orchestrator not initialized" << std::endl).str());
        return false;
    }
    if (!maintainDataStorage())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore start recording while data storage is not ready"
            << " disk_root=" << options_.diskRoot
            << std::endl).str());
        setLedState(LedState::WaitStorage);
        return false;
    }
    if (isEgoRequired() && !egoBoundConnected_)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore start recording while bound ego is disconnected"
            << " serial=" << egoBoundSerial_ << std::endl).str());
        setEgoBindingFault("bound ego is disconnected");
        return false;
    }
    if (isStereoStartupWaiting())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore start recording while Fays stereo recorder is still starting"
            << std::endl).str());
        setLedState(LedState::Init);
        return false;
    }
    if (const std::optional<DA::Ugripper::HealthFault> fault = currentHealthFault())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore start recording while hardware health fault is present"
            << " key=" << fault->key
            << std::endl).str());
        setHardwareFaultLedState(*fault);
        sendAudioCommand("error");
        return false;
    }
    maintainDeviceVersions();
    DeviceVersions versions = deviceVersions_;
    versions["hardware_version"] = hardwareVersion_;
    versions["software_version"] = packageVersion_;
    versions["das_usb_updater_version"] = updaterVersion_;
    const auto missing = MissingRequiredDeviceVersions(versions, chestCameraEnabled_, stereoEnabled_, !egoBoundSerial_.empty());
    if (!missing.empty())
    {
        DM_LOG_WARN("recording rejected: versions not ready: {}", joinStrings(missing, ", "));
        return false;
    }
    if (episodeManager_ == nullptr) return false;
    episodeManager_->SetDeviceVersions(deviceVersions_, egoBoundSerial_);
    cancelBackgroundTactileValidation();
    tactileTriggeredAudioCommand_.clear();
    activeHardwareFault_.reset();
    resetRestoreUsbErrorWindow();
    taskMarkerTracker_.Reset();
    recordingRightUpPressedSinceMs_ = 0;
    recordingRightUpHandled_ = false;
    return recordingOrchestrator_->StartRecording(resetRecording, nullptr);
}

bool RecordRuntime::stopRecording(bool dueToError, const std::string &reason, const std::string &errorType)
{
    if (recordingOrchestrator_ == nullptr)
    {
        DM_LOG_ERROR_CODE("UGR-00-01", "{}", (::DA::utils::LogString() << "recording orchestrator not initialized" << std::endl).str());
        return false;
    }
    const bool ok = recordingOrchestrator_->StopRecording(dueToError, reason, errorType, nullptr);
    const std::string completedEpisodeDir = lastEpisodeDir();
    if (!dueToError && activeHardwareFault_.has_value() && IsCameraRecorderMainCameraFault(*activeHardwareFault_))
    {
        maybeTriggerRestoreUsbForHardwareFault(*activeHardwareFault_);
    }
    if (ok && !dueToError && !completedEpisodeDir.empty())
    {
        scheduleBackgroundTactileValidation(completedEpisodeDir);
    }
    if (dueToError && activeHardwareFault_.has_value())
    {
        setHardwareFaultLedState(*activeHardwareFault_);
    }
    if (ok && !dueToError)
    {
        activeHardwareFault_.reset();
        if (!tactileTriggeredAudioCommand_.empty())
        {
            setAudioRecoveryCommand(tactileWarningActive_ ? "" : tactileTriggeredAudioCommand_);
            sendAudioCommand(tactileTriggeredAudioCommand_);
        }
        else if (!tactileWarningActive_)
        {
            setAudioRecoveryCommand("ready");
        }
        applyIdleState();
    }
    return ok;
}

bool RecordRuntime::handleShortUpAction()
{
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "BTN_UP short press" << std::endl).str());
    if (!isRecordingActive())
    {
        if (healthState_.status == DA::Ugripper::HealthStatus::Error)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "ignore start recording while hardware health is in error state" << std::endl).str());
            sendAudioCommand("error");
            return false;
        }
        return startRecording(false);
    }
    return stopRecording(false, "BTN_UP short stop");
}

bool RecordRuntime::handleShortDownAction()
{
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "BTN_DOWN short press" << std::endl).str());
    if (!isRecordingActive())
    {
        if (healthState_.status == DA::Ugripper::HealthStatus::Error)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "ignore reset recording while hardware health is in error state" << std::endl).str());
            sendAudioCommand("error");
            return false;
        }
        if (lastEpisodeDir().empty() || !fs::exists(lastEpisodeDir()))
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString() << "no previous episode, BTN_DOWN reset ignored" << std::endl).str());
            setAudioRecoveryCommand(tactileWarningActive_ ? "" : "ready");
            sendAudioCommand("no_reset_needed");
            applyIdleState();
            return false;
        }
        return startRecording(true);
    }
    return stopRecording(false, "BTN_DOWN short stop");
}

bool RecordRuntime::handlePhysicalRecordShortPress(DA::Ugripper::HmiEventType eventType)
{
    const uint64_t nowMs = currentSteadyMs();
    const char *eventName = hmiEventName(eventType);
    if (hasPendingPhysicalRecordShortPress_)
    {
        const uint64_t elapsedMs = nowMs >= pendingPhysicalRecordShortPressMs_
                                       ? nowMs - pendingPhysicalRecordShortPressMs_
                                       : 0;
        if (pendingPhysicalRecordShortPress_ == eventType && elapsedMs <= kPhysicalRecordDoubleClickMs)
        {
            hasPendingPhysicalRecordShortPress_ = false;
            pendingPhysicalRecordShortPressMs_ = 0;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "[HMI_DIAG] category=record_double_click"
                << " event=" << eventName
                << " result=accepted"
                << " elapsed_ms=" << elapsedMs
                << " window_ms=" << kPhysicalRecordDoubleClickMs).str());
            if (eventType == DA::Ugripper::HmiEventType::ShortUpPressed)
            {
                return handleShortUpAction();
            }
            return handleShortDownAction();
        }

        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "[HMI_DIAG] category=record_double_click"
            << " event=" << eventName
            << " result=cancel_previous"
            << " previous_event=" << hmiEventName(pendingPhysicalRecordShortPress_)
            << " elapsed_ms=" << elapsedMs
            << " window_ms=" << kPhysicalRecordDoubleClickMs).str());
    }

    hasPendingPhysicalRecordShortPress_ = true;
    pendingPhysicalRecordShortPress_ = eventType;
    pendingPhysicalRecordShortPressMs_ = nowMs;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "[HMI_DIAG] category=record_double_click"
        << " event=" << eventName
        << " result=armed"
        << " window_ms=" << kPhysicalRecordDoubleClickMs).str());
    return false;
}

void RecordRuntime::clearPendingPhysicalRecordShortPress(const char *reason, uint64_t nowMs)
{
    if (!hasPendingPhysicalRecordShortPress_)
    {
        return;
    }
    const uint64_t elapsedMs = nowMs >= pendingPhysicalRecordShortPressMs_
                                   ? nowMs - pendingPhysicalRecordShortPressMs_
                                   : 0;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "[HMI_DIAG] category=record_double_click"
        << " result=cleared"
        << " reason=" << (reason == nullptr ? "" : reason)
        << " pending_event=" << hmiEventName(pendingPhysicalRecordShortPress_)
        << " elapsed_ms=" << elapsedMs
        << " window_ms=" << kPhysicalRecordDoubleClickMs).str());
    hasPendingPhysicalRecordShortPress_ = false;
    pendingPhysicalRecordShortPressMs_ = 0;
}

bool RecordRuntime::handleLongUpAction()
{
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "BTN_UP long press" << std::endl).str());
    if (isRecordingActive())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "ignore pre-audio while recording" << std::endl).str());
        return false;
    }
    return recordAudioClip("pre", true);
}

bool RecordRuntime::handleLongDownAction()
{
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "BTN_DOWN long press" << std::endl).str());
    if (isRecordingActive())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "ignore post-audio while recording" << std::endl).str());
        return false;
    }
    return recordAudioClip("post", false);
}

void RecordRuntime::handleButtonAction(DA::Ugripper::ButtonAction action)
{
    switch (action)
    {
    case DA::Ugripper::ButtonAction::ToggleEgoBinding:
        handleEgoBindingToggle();
        break;
    }
}

void RecordRuntime::handleRecordingTaskMarker()
{
    const double unixSeconds = static_cast<double>(currentEpochMs()) / 1000.0;
    const auto result = taskMarkerTracker_.Mark(unixSeconds);
    switch (result)
    {
    case DA::Ugripper::TaskMarkerResult::StartMarked:
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "recording task start marker"
            << " task_start_s=" << unixSeconds << std::endl).str());
        playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::TaskStartMarked);
        break;
    case DA::Ugripper::TaskMarkerResult::StopMarked:
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "recording task stop marker"
            << " task_stop_s=" << unixSeconds << std::endl).str());
        playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::TaskStopMarked);
        break;
    case DA::Ugripper::TaskMarkerResult::Rejected:
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore recording task marker after start and stop were already marked"
            << std::endl).str());
        playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::TaskMarkerRejected);
        break;
    }
}

void RecordRuntime::playHmiFeedback(DA::Ugripper::HmiFeedbackEvent event)
{
    const auto pattern = DA::Ugripper::FeedbackPatternFor(event);
    const auto setEnabled = [this, &pattern](bool enabled, uint16_t frequencyHz) {
        if (pattern.led_side == DA::Ugripper::HmiFeedbackSide::Both)
        {
            panelManager_.setLedColor(
                enabled ? pattern.red : 0,
                enabled ? pattern.green : 0,
                enabled ? pattern.blue : 0);
        }
        else
        {
            panelManager_.setLedColorForSide(
                "right",
                enabled ? pattern.red : 0,
                enabled ? pattern.green : 0,
                enabled ? pattern.blue : 0);
        }

        const GripperBeepState beepState = enabled
                                               ? GripperBeepState{
                                                     GripperHmiDriver::kDefaultBeepDuty,
                                                     frequencyHz,
                                                 }
                                               : GripperBeepState{0, 0};
        if (pattern.beep_side == DA::Ugripper::HmiFeedbackSide::Both)
        {
            panelManager_.setBeepStateForSide("left", beepState);
            panelManager_.setBeepStateForSide("right", beepState);
        }
        else
        {
            panelManager_.setBeepStateForSide("right", beepState);
        }
    };

    for (int pulse = 0; pulse < pattern.pulse_count; ++pulse)
    {
        const uint16_t configuredFrequency =
            pulse < static_cast<int>(pattern.beep_frequencies_hz.size())
                ? pattern.beep_frequencies_hz[static_cast<size_t>(pulse)]
                : 0;
        const uint16_t frequencyHz = configuredFrequency == 0
                                         ? GripperHmiDriver::kDefaultBeepFrequency
                                         : configuredFrequency;
        setEnabled(true, frequencyHz);
        std::this_thread::sleep_for(std::chrono::milliseconds(pattern.on_ms));
        setEnabled(false, 0);
        if (pulse + 1 < pattern.pulse_count && pattern.off_ms > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(pattern.off_ms));
        }
    }
    const bool confirmBindingFeedbackSilence =
        event == DA::Ugripper::HmiFeedbackEvent::EgoBound ||
        event == DA::Ugripper::HmiFeedbackEvent::EgoUnbound ||
        event == DA::Ugripper::HmiFeedbackEvent::EgoBindingFailed;
    if (confirmBindingFeedbackSilence)
    {
        if (pattern.beep_side == DA::Ugripper::HmiFeedbackSide::Both)
        {
            panelManager_.silenceBeepForSide("left");
            panelManager_.silenceBeepForSide("right");
        }
        else
        {
            panelManager_.silenceBeepForSide("right");
        }
    }
    if (isRecordingActive())
    {
        setLedState(LedState::Recording);
    }
    else
    {
        applyIdleState();
    }
}

void RecordRuntime::handleDualShutdownAction()
{
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "dual-button shutdown requested" << std::endl).str());
    if (isRecordingActive())
    {
        stopRecording(false, "dual-button shutdown");
    }
    setLedState(LedState::Exit);
    setAudioRecoveryCommand("writing");
    sendAudioCommand("writing");

    std::string errorMessage;
    if (shutdownRequestPort_ == nullptr ||
        !shutdownRequestPort_->RequestAction("shutdown", &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString() << "failed to request shutdown action: "
                              << options_.systemActionRequestFile
                              << ", error=" << errorMessage << std::endl).str());
        setLedState(LedState::Error5);
        return;
    }
    stopRequested_.store(true);
}

bool RecordRuntime::handleLeftDualUmountAction()
{
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "left dual-button umount requested" << std::endl).str());
    if (isRecordingActive())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "ignore left dual-button umount while recording" << std::endl).str());
        setAudioRecoveryCommand("recording");
        sendAudioCommand("error");
        return false;
    }

    setLedState(LedState::Writing);
    setAudioRecoveryCommand("writing");
    sendAudioCommand("writing");
    syncRuntimeLogToDisk("left dual-button umount");

    std::string errorMessage;
    if (shutdownRequestPort_ == nullptr ||
        !shutdownRequestPort_->RequestAction("umount", &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString() << "failed to request umount: " << errorMessage << std::endl).str());
        setAudioRecoveryCommand(tactileWarningActive_ ? "" : "ready");
        applyIdleState();
        sendAudioCommand("error");
        return false;
    }

    std::string result;
    if (!shutdownRequestPort_->WaitForActionResult(
            &result,
            static_cast<int>(kSystemActionResultWaitMs),
            &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString() << "failed to wait for umount result: " << errorMessage << std::endl).str());
        setAudioRecoveryCommand(tactileWarningActive_ ? "" : "ready");
        applyIdleState();
        sendAudioCommand("error");
        return false;
    }

    if (result == "ok")
    {
        setAudioRecoveryCommand("umount");
        sendAudioCommand("umount");
        std::this_thread::sleep_for(std::chrono::milliseconds(kPostUmountAudioDelayMs));
        stopRequested_.store(true);
        return true;
    }

    DM_LOG_ERROR_CODE("UGR-80-01", "{}", (::DA::utils::LogString() << "umount request failed with result: " << result << std::endl).str());
    setAudioRecoveryCommand(tactileWarningActive_ ? "" : "ready");
    applyIdleState();
    sendAudioCommand("error");
    return false;
}

bool RecordRuntime::markEpisodeOperatorFailed(const std::string &episodeDir, std::string *errorMessage) const
{
    if (episodeDir.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "no completed episode available";
        }
        return false;
    }

    const fs::path episodePath(episodeDir);
    const fs::path metadataPath = episodePath / "metadata.json";
    std::ifstream input(metadataPath);
    if (!input.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metadata.json not found: " + metadataPath.string();
        }
        return false;
    }

    ordered_json metadata;
    try
    {
        input >> metadata;
    }
    catch (const std::exception &ex)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string("failed to parse metadata.json: ") + ex.what();
        }
        return false;
    }
    if (!metadata.is_object())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metadata.json top-level value must be an object";
        }
        return false;
    }

    metadata["quality_check_status"] = "fail";
    metadata["quality_check_err_type"] = DA::Ugripper::kErrorTypeOperatorMarkedFailed;

    std::string writeError;
    if (!writeTextFileAtomically(metadataPath, metadata.dump(2) + "\n", &writeError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to write metadata.json: " + writeError;
        }
        return false;
    }

    std::error_code flushError;
    if (!flushFileToDisk(metadataPath, &flushError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to flush metadata.json: " + flushError.message();
        }
        return false;
    }

    writeValidationErrorLog(episodePath, "operator marked episode failed by left-hand single long press");
    if (!flushDirectoryToDisk(episodePath, &flushError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to flush episode directory after operator mark: " + flushError.message();
        }
        return false;
    }
    return true;
}

void RecordRuntime::playOperatorMarkFailedFeedback()
{
    for (int pulse = 0; pulse < 2; ++pulse)
    {
        panelManager_.setLedColor(kOperatorMarkLedRed, kOperatorMarkLedGreen, kOperatorMarkLedBlue);
        panelManager_.setBeepEnabledForSide("left", true);
        panelManager_.setBeepEnabledForSide("right", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(kOperatorMarkBeepOnMs));
        panelManager_.turnOff();
        for (int attempt = 0; attempt < kOperatorMarkBeepSilenceRetryAttempts; ++attempt)
        {
            panelManager_.silenceBeepForSide("left");
            panelManager_.silenceBeepForSide("right");
            if (attempt + 1 < kOperatorMarkBeepSilenceRetryAttempts)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(kOperatorMarkBeepSilenceRetryIntervalMs));
            }
        }
        if (pulse == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kOperatorMarkBeepOffMs));
        }
    }
    applyIdleState();
}

bool RecordRuntime::handleLeftSingleLongMarkFailedAction()
{
    if (isRecordingActive())
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "ignore operator fail mark while recording" << std::endl).str());
        sendAudioCommand("error");
        return false;
    }

    const std::string episodeDir = lastEpisodeDir();
    std::string errorMessage;
    if (!markEpisodeOperatorFailed(episodeDir, &errorMessage))
    {
        DM_LOG_ERROR_CODE("UGR-10-02", "{}", (::DA::utils::LogString()
            << "failed to mark episode as operator failed"
            << " episode_dir=" << episodeDir
            << " error=" << errorMessage << std::endl).str());
        sendAudioCommand("error");
        return false;
    }

    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "episode marked as operator failed"
        << " episode_dir=" << episodeDir << std::endl).str());
    playOperatorMarkFailedFeedback();
    syncRuntimeLogToDisk("operator mark failed");
    return true;
}

bool RecordRuntime::recordAudioClip(const std::string &audioType, bool monitorUpButton)
{
    if (!fs::exists(options_.audioRecordScript))
    {
        DM_LOG_ERROR_CODE("UGR-70-01", "{}", (::DA::utils::LogString() << "audio record script not found: " << options_.audioRecordScript << std::endl).str());
        setAudioRecoveryCommand("error");
        sendAudioCommand("error");
        return false;
    }

    const std::string timestamp = makeTimestampString();
    const fs::path tempDir(options_.audioTempDir);
    const fs::path captureFile = tempDir / ("audio_" + audioType + "_" + timestamp + "_capture.wav");
    const fs::path ch1File = tempDir / ("audio_" + audioType + "_" + timestamp + "_ch1.wav");
    const fs::path ch1Denoised = tempDir / ("audio_" + audioType + "_" + timestamp + "_ch1_denoised.wav");
    const fs::path outputFile = tempDir / ("audio_" + audioType + "_" + timestamp + ".wav");

    sendAudioCommand(audioType == "pre" ? "pre_audio_recording" : "post_audio_recording");

    DA::Ugripper::SubprocessHandle audioRecorder("audio_recorder");
    std::vector<std::string> audioRecorderArgs = resolvePythonCommand();
    audioRecorderArgs.push_back(options_.audioRecordScript);
    audioRecorderArgs.push_back("--output");
    audioRecorderArgs.push_back(captureFile.string());
    if (!audioRecorder.Start(audioRecorderArgs))
    {
        DM_LOG_ERROR_CODE("UGR-50-03", "{}", (::DA::utils::LogString() << "failed to start audio recorder" << std::endl).str());
        sendAudioCommand("audio_recording_stop");
        return false;
    }

    const auto finishAudioRecording = [&](bool ok) {
        sendAudioCommand("audio_recording_stop");
        syncRuntimeLogToDisk(audioType == "pre" ? "pre-audio stop" : "post-audio stop");
        return ok;
    };

    while (!stopRequested_.load())
    {
        ButtonSnapshot buttons;
        panelManager_.poll(options_.pollMs, &buttons);
        const bool stillPressed = monitorUpButton ? buttons.upPressed : buttons.downPressed;
        if (!stillPressed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audioRecorder.SendSignal(SIGINT);
    if (!audioRecorder.Wait(5000))
    {
        audioRecorder.Stop(DA::Ugripper::ProcessStopMode::SigTermThenKill, 1000, nullptr);
    }

    if (!fileExistsAndNotEmpty(captureFile.string()) || fs::file_size(captureFile) <= 44)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "audio capture is empty" << std::endl).str());
        fs::remove(captureFile);
        return finishAudioRecording(false);
    }

    bool ok = false;
    if (runCommandSync({"sox", captureFile.string(), ch1File.string(), "remix", "1"}))
    {
        if (fs::exists(options_.noiseProfile) &&
            runCommandSync({"sox", ch1File.string(), ch1Denoised.string(), "noisered", options_.noiseProfile, "0.15"}) &&
            runCommandSync({"sox", ch1Denoised.string(), outputFile.string(), "norm"}))
        {
            ok = true;
        }
        else if (runCommandSync({"sox", ch1File.string(), outputFile.string(), "norm"}))
        {
            ok = true;
        }
    }

    fs::remove(captureFile);
    fs::remove(ch1File);
    fs::remove(ch1Denoised);

    if (!ok || !fileExistsAndNotEmpty(outputFile.string()))
    {
        DM_LOG_ERROR_CODE("UGR-50-05", "{}", (::DA::utils::LogString() << "failed to process audio clip" << std::endl).str());
        fs::remove(outputFile);
        return finishAudioRecording(false);
    }

    if (audioType == "pre")
    {
        pendingPreAudioFile_ = outputFile.string();
        DM_LOG_INFO("{}", (::DA::utils::LogString() << "pre-audio prepared: " << pendingPreAudioFile_ << std::endl).str());
    }
    else
    {
        if (!lastEpisodeDir().empty() && fs::exists(lastEpisodeDir()))
        {
            std::error_code error;
            if (!moveFileWithCrossDeviceFallback(outputFile, fs::path(lastEpisodeDir()) / "audio_post.wav", &error))
            {
                DM_LOG_ERROR_CODE("UGR-50-05", "{}", (::DA::utils::LogString() << "failed to move post-audio into last episode: " << error.message() << std::endl).str());
                fs::remove(outputFile);
            }
            else if (!flushFileToDisk(fs::path(lastEpisodeDir()) / "audio_post.wav", &error))
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush post-audio into last episode: " << error.message() << std::endl).str());
            }
        }
        else
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "no previous episode for post-audio" << std::endl).str());
            fs::remove(outputFile);
        }
    }

    return finishAudioRecording(true);
}

void RecordRuntime::handleButtons(const ButtonSnapshot &buttons)
{
    if (hmiController_ == nullptr)
    {
        return;
    }

    if (isRecordingActive() && buttons.upPressed && !buttons.downPressed)
    {
        const uint64_t nowMs = currentSteadyMs();
        if (recordingRightUpPressedSinceMs_ == 0)
        {
            recordingRightUpPressedSinceMs_ = nowMs;
            recordingRightUpHandled_ = false;
        }
        const uint64_t heldMs = nowMs >= recordingRightUpPressedSinceMs_
                                    ? nowMs - recordingRightUpPressedSinceMs_
                                    : 0;
        if (!recordingRightUpHandled_ && heldMs >= kRecordingTaskMarkThresholdMs)
        {
            recordingRightUpHandled_ = true;
            clearPendingPhysicalRecordShortPress("task_marker", nowMs);
            hmiController_->Reset();
            handleRecordingTaskMarker();
        }
        if (recordingRightUpHandled_)
        {
            return;
        }
    }
    else
    {
        recordingRightUpPressedSinceMs_ = 0;
        recordingRightUpHandled_ = false;
    }

    if (!hasLastLoggedButtons_ ||
        lastLoggedButtons_.upPressed != buttons.upPressed ||
        lastLoggedButtons_.downPressed != buttons.downPressed)
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "[HMI_DIAG] category=button_raw"
            << " up=" << boolText(buttons.upPressed)
            << " down=" << boolText(buttons.downPressed)).str());
        lastLoggedButtons_ = buttons;
        hasLastLoggedButtons_ = true;
    }

    const auto events = hmiController_->HandleButtons(
        DA::Ugripper::ButtonSnapshot{.up_pressed = buttons.upPressed, .down_pressed = buttons.downPressed});
    for (const auto &event : events)
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "[HMI_DIAG] category=button_event"
            << " event=" << hmiEventName(event.type)
            << " up=" << boolText(buttons.upPressed)
            << " down=" << boolText(buttons.downPressed)).str());
        switch (event.type)
        {
        case DA::Ugripper::HmiEventType::ShortUpPressed:
            handlePhysicalRecordShortPress(event.type);
            break;
        case DA::Ugripper::HmiEventType::ShortDownPressed:
            handlePhysicalRecordShortPress(event.type);
            break;
        case DA::Ugripper::HmiEventType::LongUpPressed:
            clearPendingPhysicalRecordShortPress("long_up", currentSteadyMs());
            handleLongUpAction();
            break;
        case DA::Ugripper::HmiEventType::LongDownPressed:
            clearPendingPhysicalRecordShortPress("long_down", currentSteadyMs());
            handleLongDownAction();
            break;
        case DA::Ugripper::HmiEventType::ShutdownPromptRequested:
            clearPendingPhysicalRecordShortPress("shutdown_prompt", currentSteadyMs());
            DM_LOG_INFO("{}", (::DA::utils::LogString() << "dual-button chord armed" << std::endl).str());
            sendAudioCommand("shutdown");
            break;
        case DA::Ugripper::HmiEventType::ShutdownRequested:
            clearPendingPhysicalRecordShortPress("shutdown", currentSteadyMs());
            handleDualShutdownAction();
            break;
        }
    }
}

void RecordRuntime::handleLeftButtons(const ButtonSnapshot &buttons)
{
    if (!hasLastLoggedLeftButtons_ ||
        lastLoggedLeftButtons_.upPressed != buttons.upPressed ||
        lastLoggedLeftButtons_.downPressed != buttons.downPressed)
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "[HMI_DIAG] category=button_raw_left"
            << " up=" << boolText(buttons.upPressed)
            << " down=" << boolText(buttons.downPressed)).str());
        lastLoggedLeftButtons_ = buttons;
        hasLastLoggedLeftButtons_ = true;
    }

    const uint64_t nowMs = currentSteadyMs();
    if (buttons.upPressed && buttons.downPressed)
    {
        if (!leftDualChordActive_)
        {
            leftDualChordActive_ = true;
            leftBothPressedSinceMs_ = nowMs;
            leftDualLongHandled_ = false;
            leftSinglePressedSinceMs_ = 0;
            leftSingleLongHandled_ = false;
            DM_LOG_INFO("{}", (::DA::utils::LogString() << "left dual-button chord armed" << std::endl).str());
        }

        const uint64_t dualHeldMs = nowMs - leftBothPressedSinceMs_;
        if (dualHeldMs >= kDualLongPressThresholdMs && !leftDualLongHandled_)
        {
            leftDualLongHandled_ = true;
            handleLeftDualUmountAction();
        }

        lastLeftButtons_ = buttons;
        return;
    }

    if (leftDualChordActive_)
    {
        if (!buttons.upPressed && !buttons.downPressed)
        {
            leftDualChordActive_ = false;
            leftBothPressedSinceMs_ = 0;
            leftDualLongHandled_ = false;
        }
        lastLeftButtons_ = buttons;
        return;
    }

    if (!isRecordingActive() && buttons.upPressed && !buttons.downPressed)
    {
        const bool freshSinglePress =
            (!lastLeftButtons_.upPressed && !lastLeftButtons_.downPressed) ||
            buttons.upPressed != lastLeftButtons_.upPressed ||
            buttons.downPressed != lastLeftButtons_.downPressed;
        if (freshSinglePress)
        {
            leftSinglePressedSinceMs_ = nowMs;
            leftSingleLongHandled_ = false;
        }

        const uint64_t heldMs = leftSinglePressedSinceMs_ > 0 && nowMs >= leftSinglePressedSinceMs_
                                    ? nowMs - leftSinglePressedSinceMs_
                                    : 0;
        if (heldMs >= kLongPressThresholdMs && !leftSingleLongHandled_)
        {
            leftSingleLongHandled_ = true;
            handleLeftSingleLongMarkFailedAction();
        }
        lastLeftButtons_ = buttons;
        return;
    }

    leftSinglePressedSinceMs_ = 0;
    leftSingleLongHandled_ = false;
    lastLeftButtons_ = buttons;
}

void RecordRuntime::resetLeftButtonTracking()
{
    lastLeftButtons_ = {};
    leftBothPressedSinceMs_ = 0;
    leftDualChordActive_ = false;
    leftDualLongHandled_ = false;
    leftSinglePressedSinceMs_ = 0;
    leftSingleLongHandled_ = false;
}

bool RecordRuntime::initializeRecordControlPipe()
{
    if (options_.recordControlPipe.empty())
    {
        return false;
    }

    std::string errorMessage;
    if (!recordControlReader_.Open(options_.recordControlPipe, 0620, &errorMessage))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "failed to open record control FIFO: " << options_.recordControlPipe
            << " error=" << errorMessage << std::endl).str());
        return false;
    }

    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "record control FIFO ready: " << options_.recordControlPipe << std::endl).str());
    return true;
}

void RecordRuntime::closeRecordControlPipe()
{
    recordControlReader_.Close();
}

bool RecordRuntime::pollRecordControlPipe()
{
    if (!recordControlReader_.IsOpen())
    {
        return false;
    }

    bool accepted = false;
    std::vector<std::string> commands;
    std::string errorMessage;
    if (!recordControlReader_.ReadAvailable(&commands, &errorMessage))
    {
        if (errorMessage.find("buffer overflow") != std::string::npos)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "[CONTROL_DIAG] source=fifo result=ignored reason=buffer_overflow").str());
            return false;
        }
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "record control FIFO read failed: " << errorMessage << std::endl).str());
        closeRecordControlPipe();
        return false;
    }

    for (const std::string& command : commands)
    {
        if (processRecordControlCommand(command))
        {
            accepted = true;
        }
    }
    return accepted;
}

bool RecordRuntime::processRecordControlCommand(const std::string &rawCommand)
{
    std::string command = trim(rawCommand);
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (command.empty())
    {
        return false;
    }

    const bool recording = isRecordingActive();
    std::string eventName;
    std::string action;
    if (command == "SHORT_UP")
    {
        eventName = "ShortUpPressed";
        action = "short_up";
    }
    else if (command == "SHORT_DOWN")
    {
        eventName = "ShortDownPressed";
        action = "short_down";
    }
    else if (command == "START")
    {
        if (recording)
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "[CONTROL_DIAG] source=fifo command=" << command
                << " result=ignored reason=already_recording").str());
            return false;
        }
        eventName = "ShortUpPressed";
        action = "start";
    }
    else if (command == "STOP")
    {
        if (!recording)
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "[CONTROL_DIAG] source=fifo command=" << command
                << " result=ignored reason=not_recording").str());
            return false;
        }
        eventName = "ShortDownPressed";
        action = "stop";
    }
    else
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "[CONTROL_DIAG] source=fifo command=" << command
            << " result=ignored reason=unknown_command").str());
        return false;
    }

    const uint64_t nowMs = currentSteadyMs();
    if (lastRecordControlActionMs_ > 0 && nowMs - lastRecordControlActionMs_ < kRecordControlDebounceMs)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "[CONTROL_DIAG] source=fifo command=" << command
            << " action=" << action
            << " result=ignored reason=debounce").str());
        return false;
    }

    lastRecordControlActionMs_ = nowMs;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "[CONTROL_DIAG] source=fifo command=" << command
        << " action=" << action
        << " result=accepted"
        << " mapped_event=" << eventName).str());
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "[HMI_DIAG] category=button_event"
        << " source=fifo"
        << " event=" << eventName
        << " up=0 down=0").str());

    if (eventName == "ShortUpPressed")
    {
        handleShortUpAction();
    }
    else
    {
        handleShortDownAction();
    }
    return true;
}

}  // namespace DA::Ugripper
