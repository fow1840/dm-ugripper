#include "record_runtime.h"
#include "record_runtime/ego_sync_policy.h"
#include "record_runtime/runtime_episode_internal.h"
#include "record_runtime/runtime_split_internal.h"
#include "record_runtime/runtime_support_internal.h"
#include "record_runtime/error_code_mapping.h"
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

bool RecordRuntime::startAudioPlayer()
{
    if (!fs::exists(options_.audioPlayScript))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "audio play script not found: " << options_.audioPlayScript << std::endl).str());
        return false;
    }
    std::string errorMessage;
    const bool started = audioCoordinator_ != nullptr && audioCoordinator_->StartAudioPlayer(&errorMessage);
    return started;
}

void RecordRuntime::stopAudioPlayer()
{
    if (audioCoordinator_ != nullptr)
    {
        audioCoordinator_->StopAudioPlayer();
    }
}

void RecordRuntime::setAudioRecoveryCommand(std::string command)
{
    audioRecoveryCommand_ = std::move(command);
    if (audioCoordinator_ != nullptr)
    {
        audioCoordinator_->SetRecoveryCommand(audioRecoveryCommand_);
    }
}

void RecordRuntime::maintainAudioPlayer()
{
    if (audioCoordinator_ != nullptr)
    {
        audioCoordinator_->MaintainAudioPlayer();
    }
}

bool RecordRuntime::startStereoDaemon()
{
    if (!stereoEnabled_)
    {
        return true;
    }
    std::string errorMessage;
    const bool started = stereoSessionClient_ != nullptr && stereoSessionClient_->StartDaemon(&errorMessage);
    if (!started)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to start stereo daemon"
                             << (errorMessage.empty() ? "" : (": " + errorMessage))
                             << std::endl).str());
    }
    return started;
}

void RecordRuntime::stopStereoDaemon()
{
    if (stereoSessionClient_ != nullptr)
    {
        stereoSessionClient_->StopDaemon();
    }
}

void RecordRuntime::maintainStereoDaemon()
{
    if (restoreUsbStereoDaemonStopped_)
    {
        return;
    }
    if (stereoSessionClient_ != nullptr)
    {
        stereoSessionClient_->MaintainDaemon();
    }
}

std::optional<DA::Ugripper::HealthFault> RecordRuntime::currentHealthFault() const
{
    if (healthMonitor_ == nullptr)
    {
        return std::nullopt;
    }
    return healthMonitor_->EvaluateHealth();
}

bool RecordRuntime::isStereoStartupWaiting() const
{
    if (!stereoEnabled_)
    {
        return false;
    }
    const uint64_t nowMs = currentSteadyMs();
    const uint64_t firstSeenMs = healthState_.first_seen_ms == 0 ? nowMs : healthState_.first_seen_ms;
    if ((nowMs - firstSeenMs) >= kStereoStartupReadyTimeoutMs)
    {
        return false;
    }

    const std::optional<DA::Ugripper::HealthFault> fault = currentHealthFault();
    return fault.has_value() && IsStereoStartupFaultKey(fault->key);
}

void RecordRuntime::suspendStereoDaemonForRestoreUsb(const std::string &reason)
{
    if (!stereoEnabled_)
    {
        return;
    }
    if (restoreUsbStereoDaemonStopped_)
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "stereo daemon already suspended for restore usb"
            << " reason=" << reason
            << std::endl).str());
        return;
    }

    restoreUsbStereoDaemonStopped_ = true;
    DM_LOG_WARN("{}", (::DA::utils::LogString()
        << "suspend stereo daemon before restore usb"
        << " reason=" << reason
        << std::endl).str());
    stopStereoDaemon();
}

void RecordRuntime::resumeStereoDaemonAfterRestoreUsb(const std::string &reason)
{
    if (!stereoEnabled_)
    {
        return;
    }
    if (!restoreUsbStereoDaemonStopped_)
    {
        return;
    }

    restoreUsbStereoDaemonStopped_ = false;
    DM_LOG_WARN("{}", (::DA::utils::LogString()
        << "resume stereo daemon after restore usb"
        << " reason=" << reason
        << std::endl).str());
    startStereoDaemon();
}

bool RecordRuntime::writeStereoControl(bool recording,
                                       const std::string &episodeDir,
                                       int64_t startSystemTimeUs,
                                       int64_t stopSystemTimeUs)
{
    if (!stereoEnabled_)
    {
        return true;
    }
    std::string errorMessage;
    bool ok = false;
    if (stereoSessionClient_ != nullptr)
    {
        if (recording)
        {
            ok = stereoSessionClient_->StartSession(episodeDir, startSystemTimeUs, &errorMessage);
        }
        else
        {
            ok = stereoSessionClient_->StopSession(episodeDir, stopSystemTimeUs, &errorMessage);
        }
    }
    if (!ok)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to write stereo control command: " << errorMessage << std::endl).str());
    }
    return ok;
}

bool RecordRuntime::waitForStereoFinalize(const std::string &episodeDir, int timeoutMs, std::string *errorMessage)
{
    if (!stereoEnabled_)
    {
        return true;
    }
    if (stereoSessionClient_ == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "stereo session client not initialized";
        }
        return false;
    }
    return stereoSessionClient_->WaitForFinalize(episodeDir, timeoutMs, errorMessage);
}

std::vector<std::string> egoWorkerArgs(const std::string &script,
                                       const std::string &command,
                                       const std::string &episodeDir,
                                       const std::string &cameraCodec = "",
                                       const std::string &serial = "")
{
    std::vector<std::string> args = resolvePythonCommand();
    args.push_back(script);
    args.push_back(command);
    args.push_back("--episode-dir");
    args.push_back(episodeDir);
    args.push_back("--status-file");
    args.push_back((fs::path(episodeDir) / "ego" / kEgoSyncStatusFileName).string());
    if (!serial.empty())
    {
        args.push_back("--serial");
        args.push_back(serial);
    }
    if (command == "start" && !cameraCodec.empty())
    {
        args.push_back("--codec");
        args.push_back(cameraCodec);
    }
    return args;
}

std::vector<std::string> egoBindingWorkerArgs(const std::string &script,
                                              const std::string &command,
                                              const std::string &bindingFile,
                                              const std::string &statusFile)
{
    std::vector<std::string> args = resolvePythonCommand();
    args.push_back(script);
    args.push_back(command);
    args.push_back("--binding-file");
    args.push_back(bindingFile);
    args.push_back("--binding-status-file");
    args.push_back(statusFile);
    return args;
}

void RecordRuntime::startEgoFeedback()
{
    const auto script = options_.egoRecordingScript;
    const auto binding = options_.egoBindingFile;
    const auto status = options_.egoBindingStatusFile;
    egoFeedback_ = std::make_unique<EgoFeedback>(options_.diskRoot, [script, binding, status](int code) {
        if (!fs::exists(script)) return;
        std::string serial;
        std::ifstream input(binding);
        if (input) {
            try { json value; input >> value; serial = value.value("serial", ""); }
            catch (const std::exception&) { return; }
        }
        if (serial.empty()) return;
        auto args = egoBindingWorkerArgs(script, "speech", binding, status);
        args.insert(args.end(), {"--serial", serial, "--speech-code", std::to_string(code)});
        if (!RecordRuntime::runCommandSync(args))
            DM_LOG_WARN("ego speech {} request failed", code);
    });
}

bool RecordRuntime::startEgoRecording(const std::string &episodeDir,
                                      int64_t,
                                      std::string *errorMessage,
                                      std::string *errorType)
{
    egoRecordingAttempted_ = false;
    if (errorType != nullptr)
    {
        errorType->clear();
    }
    if (!fs::exists(options_.egoRecordingScript))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ego recording script not found: " + options_.egoRecordingScript;
        }
        return false;
    }

    if (!hasAdbUsbInterface())
    {
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "skip ego recording sidecar: no ADB USB interface detected"
            << std::endl).str());
        if (isEgoRequired() && errorMessage != nullptr)
        {
            *errorMessage = "bound ego ADB USB interface is not present";
        }
        return !isEgoRequired();
    }

    if (egoRecordingWorker_.has_value())
    {
        std::string ignoredError;
        egoRecordingWorker_->Stop(
            DA::Ugripper::ProcessStopMode::SigTermThenKill,
            1000,
            &ignoredError);
        egoRecordingWorker_.reset();
    }

    egoRecordingWorker_.emplace("ego_recording_worker");
    egoRecordingAttempted_ = true;
    if (!egoRecordingWorker_->Start(egoWorkerArgs(options_.egoRecordingScript,
                                                 "start",
                                                 episodeDir,
                                                 options_.cameraCodec,
                                                 egoBoundSerial_),
                                   {},
                                   errorMessage))
    {
        egoRecordingWorker_.reset();
        egoRecordingAttempted_ = false;
        return false;
    }
    const fs::path statusPath = fs::path(episodeDir) / "ego" / kEgoSyncStatusFileName;
    const uint64_t startMs = currentSteadyMs();
    json lastStatus = json::object();
    auto stopEgoStartupWorker = [this]() {
        if (!egoRecordingWorker_.has_value())
        {
            return;
        }
        std::string ignoredError;
        egoRecordingWorker_->Stop(
            DA::Ugripper::ProcessStopMode::SigTermThenKill,
            1000,
            &ignoredError);
        egoRecordingWorker_.reset();
        egoRecordingAttempted_ = false;
    };
    uint64_t startAckWaitMs = kEgoStartAckWaitMs;
    while ((currentSteadyMs() - startMs) < startAckWaitMs)
    {
        json status = json::object();
        std::string loadError;
        if (loadJsonFile(statusPath.string(), &status, &loadError) && status.is_object())
        {
            lastStatus = status;
            const std::string state = status.value("status", std::string());
            if (state == "recovering")
            {
                startAckWaitMs = 60000;
            }
            if (state == "time_sync_failed")
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = status.value("error", std::string("ego time sync failed"));
                }
                if (errorType != nullptr)
                {
                    *errorType = status.value(
                        "error_type", std::string(DA::Ugripper::kErrorTypeEgoTimeSyncFailed));
                }
                stopEgoStartupWorker();
                return false;
            }
            if (state == "recording")
            {
                const std::string remoteEpisode = status.value("remote_episode", std::string());
                if (!remoteEpisode.empty())
                {
                    return true;
                }
            }
            if (status.contains("start_broadcast_returncode"))
            {
                const int returnCode = status.value("start_broadcast_returncode", -1);
                if (returnCode != 0)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = status.value("error", std::string("ego START_RECORDING broadcast failed"));
                    }
                    return false;
                }
            }
            if (state == "not_found")
            {
                if (isEgoRequired())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = status.value("error", std::string("bound ego not found"));
                    }
                    return false;
                }
                return true;
            }
            if (state == "start_timeout")
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = status.value("error", std::string("ego app did not create episode_*-temp"));
                }
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "ego recording sidecar did not report remote episode before gripper start: "
                    << status.value("error", std::string("start_timeout"))
                    << std::endl).str());
                return !isEgoRequired();
            }
            if (state == "start_failed" || state == "error")
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = status.value("error", state);
                }
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kEgoStartAckPollMs));
    }

    json timeSync = lastStatus.value("time_sync", json::object());
    if (timeSync.is_object() && timeSync.value("status", std::string()) == "ok")
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "timed out waiting for ego remote episode status";
        }
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ego recording sidecar did not report remote episode before gripper start"
            << std::endl).str());
        return !isEgoRequired();
    }

    stopEgoStartupWorker();
    const bool hasOffset = timeSync.is_object() && timeSync.contains("final_offset_ms") &&
                           timeSync["final_offset_ms"].is_number();
    const std::string offsetText = hasOffset
                                       ? std::to_string(timeSync["final_offset_ms"].get<double>())
                                       : std::string("unknown");
    const std::string syncError = "ego time sync failed offset_ms=" + offsetText +
                                  " threshold_ms=15";
    if (!timeSync.is_object())
    {
        timeSync = json::object();
    }
    timeSync["status"] = "error";
    timeSync["threshold_ms"] = 15.0;
    timeSync["error"] = syncError;
    if (!hasOffset)
    {
        timeSync["final_offset_ms"] = nullptr;
    }
    lastStatus["status"] = "time_sync_failed";
    lastStatus["error_type"] = DA::Ugripper::kErrorTypeEgoTimeSyncFailed;
    lastStatus["error"] = syncError;
    lastStatus["time_sync"] = std::move(timeSync);
    std::string writeError;
    if (!writeTextFileAtomically(statusPath, lastStatus.dump(2) + "\n", &writeError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "failed to write ego time sync timeout status: " << writeError
            << std::endl).str());
    }
    DM_LOG_ERROR_CODE("UGR-82-02", "{}", (::DA::utils::LogString()
        << syncError << std::endl).str());
    if (errorMessage != nullptr)
    {
        *errorMessage = syncError;
    }
    if (errorType != nullptr)
    {
        *errorType = DA::Ugripper::kErrorTypeEgoTimeSyncFailed;
    }
    return false;
}

bool RecordRuntime::stopEgoRecording(const std::string &episodeDir,
                                     int64_t,
                                     std::string *errorMessage)
{
    const auto workerStopStartMs = currentSteadyMs();
    if (egoRecordingWorker_.has_value())
    {
        std::string ignoredError;
        egoRecordingWorker_->Stop(
            DA::Ugripper::ProcessStopMode::SigTermThenKill,
            1500,
            &ignoredError);
        egoRecordingWorker_.reset();
    }
    DM_LOG_INFO("[STOP_DIAG] ego incremental worker stopped: elapsed_ms={}",
                currentSteadyMs() - workerStopStartMs);

    if (!fs::exists(options_.egoRecordingScript))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ego recording script not found: " + options_.egoRecordingScript;
        }
        return false;
    }

    const auto stopStartMs = currentSteadyMs();
    const bool stopped = runCommandSync(egoWorkerArgs(
        options_.egoRecordingScript, "stop", episodeDir, "", egoBoundSerial_));
    DM_LOG_INFO("[STOP_DIAG] ego stop worker finished: ok={} elapsed_ms={}",
                stopped, currentSteadyMs() - stopStartMs);
    return stopped;
}

bool RecordRuntime::cleanupEgoRemote(const std::string &episodeDir,
                                     std::string *errorMessage)
{
    if (!egoRecordingAttempted_)
    {
        return true;
    }
    if (!fs::exists(options_.egoRecordingScript))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ego recording script not found: " + options_.egoRecordingScript;
        }
        return false;
    }
    return runCommandSync(egoWorkerArgs(
        options_.egoRecordingScript, "cleanup", episodeDir, "", egoBoundSerial_));
}

bool RecordRuntime::loadEgoBinding()
{
    egoBoundSerial_.clear();
    json binding = json::object();
    std::string errorMessage;
    if (!loadJsonFile(options_.egoBindingFile, &binding, &errorMessage) || !binding.is_object())
    {
        egoBoundConnected_ = true;
        if (!clearEgoBindingState())
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "failed to publish unbound ego status while loading binding" << std::endl).str());
        }
        return false;
    }
    egoBoundSerial_ = binding.value("serial", std::string());
    if (egoBoundSerial_.empty())
    {
        egoBoundConnected_ = true;
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore invalid ego binding without serial: " << options_.egoBindingFile
            << std::endl).str());
        if (!clearEgoBindingState())
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "failed to clear invalid ego binding state" << std::endl).str());
        }
        return false;
    }
    egoBoundConnected_ = false;
    egoProbeFailureCount_ = 0;
    egoLastProbeStartMs_ = 0;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "loaded ego binding serial=" << egoBoundSerial_ << std::endl).str());
    return true;
}

bool RecordRuntime::clearEgoBindingState()
{
    if (!fs::exists(options_.egoRecordingScript))
    {
        return false;
    }
    return runCommandSync(egoBindingWorkerArgs(
        options_.egoRecordingScript,
        "unbind",
        options_.egoBindingFile,
        options_.egoBindingStatusFile));
}

bool RecordRuntime::isEgoRequired() const
{
    return !egoBoundSerial_.empty();
}

void RecordRuntime::handleEgoBindingToggle()
{
    if (isRecordingActive())
    {
        return;
    }
    if (egoBindingActionFuture_.valid() &&
        egoBindingActionFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "ignore ego binding toggle while another binding action is running"
            << std::endl).str());
        playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::EgoBindingFailed);
        return;
    }

    if (isEgoRequired())
    {
        if (!clearEgoBindingState())
        {
            DM_LOG_ERROR_CODE("UGR-82-05", "{}", (::DA::utils::LogString()
                << "failed to remove ego binding and publish unbound status" << std::endl).str());
            playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::EgoBindingFailed);
            return;
        }
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "ego binding removed serial=" << egoBoundSerial_ << std::endl).str());
        stopEgoSyncFeedback();
        egoSyncWaiting_ = false;
        egoSyncReady_ = false;
        egoBoundSerial_.clear();
        egoBoundConnected_ = true;
        egoProbeFailureCount_ = 0;
        clearEgoBindingFault();
        playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::EgoUnbound);
        return;
    }

    const std::vector<std::string> args = egoBindingWorkerArgs(
        options_.egoRecordingScript,
        "bind",
        options_.egoBindingFile,
        options_.egoBindingStatusFile);
    DM_LOG_INFO("{}", (::DA::utils::LogString() << "ego binding started" << std::endl).str());
    egoSyncReady_ = false;
    egoSyncWaiting_ = true;
    egoSyncSlowLogged_ = false;
    egoSyncFeedbackStartMs_ = currentSteadyMs();
    egoBindingActionFuture_ = std::async(std::launch::async, [args]() {
        return RecordRuntime::runCommandSync(args);
    });
    applyIdleState();
}

void RecordRuntime::maintainEgoBindingAction()
{
    if (!egoBindingActionFuture_.valid() ||
        egoBindingActionFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return;
    }
    const bool commandOk = egoBindingActionFuture_.get();
    if (commandOk && loadEgoBinding())
    {
        egoBoundConnected_ = true;
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "ego binding completed serial=" << egoBoundSerial_ << std::endl).str());
        egoSyncLastCheckMs_ = 0; // Finish feedback only when synchronization is ready.
        return;
    }
    stopEgoSyncFeedback();
    egoSyncWaiting_ = false;
    DM_LOG_ERROR_CODE("UGR-82-03", "{}",
                      (::DA::utils::LogString() << "ego binding failed" << std::endl).str());
    playHmiFeedback(DA::Ugripper::HmiFeedbackEvent::EgoBindingFailed);
}

bool RecordRuntime::isEgoSyncPending() const
{
    return egoBindingActionFuture_.valid() || (isEgoRequired() && !egoSyncReady_);
}

bool RecordRuntime::isEgoSyncReady() const
{
    json state, binding;
    std::string error, boot;
    std::ifstream bootFile("/proc/sys/kernel/random/boot_id");
    bootFile >> boot;
    return loadJsonFile("/dev/shm/ugripper/ego_time_sync.json", &state, &error) &&
           loadJsonFile(options_.egoBindingFile, &binding, &error) &&
           EgoSyncReady(state, binding, egoBoundSerial_, boot, currentSteadyMs() / 1000.0);
}

void RecordRuntime::stopEgoSyncFeedback()
{
    if (egoSyncToneOwned_)
    {
        // Driver owner repeats the OFF command five times; do not block the
        // supervisor on firmware state readback before showing ready.
        panelManager_.setBeepStateForSide("left", GripperBeepState{0, 0});
        panelManager_.setBeepStateForSide("right", GripperBeepState{0, 0});
        egoSyncToneOwned_ = false;
        egoSyncToneHz_ = 0;
    }
}

void RecordRuntime::maintainEgoSyncFeedback()
{
    const auto now = currentSteadyMs();
    if (egoSyncLastCheckMs_ == 0 || now - egoSyncLastCheckMs_ >= 100)
    {
        egoSyncLastCheckMs_ = now;
        egoSyncReady_ = isEgoRequired() && isEgoSyncReady();
    }
    const bool pending = isEgoSyncPending();
    if (pending != egoSyncWaiting_)
    {
        egoSyncWaiting_ = pending;
        if (pending)
        {
            egoSyncFeedbackStartMs_ = now;
            egoSyncSlowLogged_ = false;
        }
        else
        {
            stopEgoSyncFeedback();
            DM_LOG_INFO("ego sync ready={} elapsed_ms={}", egoSyncReady_, now - egoSyncFeedbackStartMs_);
        }
        if (!isRecordingActive()) applyIdleState();
    }
    if (pending && !egoSyncSlowLogged_ && now - egoSyncFeedbackStartMs_ >= 7000)
    {
        egoSyncSlowLogged_ = true;
        DM_LOG_WARN("ego sync exceeds 7000ms target; recording remains blocked");
    }
    // Hardware alarms and recording own their feedback; never overwrite them.
    const bool audible = pending && !isRecordingActive() && episodeManagerInitialized_ &&
                         !egoBindingFaultActive_ && !currentHealthFault().has_value();
    if (!audible)
    {
        stopEgoSyncFeedback();
        return;
    }
    const auto tone = EgoSyncTone(now - egoSyncFeedbackStartMs_);
    if (!egoSyncToneOwned_ || tone != egoSyncToneHz_)
    {
        const GripperBeepState beep = tone ? GripperBeepState{GripperHmiDriver::kDefaultBeepDuty, tone}
                                          : GripperBeepState{0, 0};
        panelManager_.setBeepStateForSide("left", beep);
        panelManager_.setBeepStateForSide("right", beep);
        egoSyncToneOwned_ = true;
        egoSyncToneHz_ = tone;
    }
}

void RecordRuntime::maintainEgoBindingHealth()
{
    if (!isEgoRequired())
    {
        if (egoProbeFuture_.valid() &&
            egoProbeFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            egoProbeFuture_.get();
            egoProbeSerial_.clear();
        }
        return;
    }

    if (egoProbeFuture_.valid() &&
        egoProbeFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        const bool connected = egoProbeFuture_.get();
        const bool matchesCurrentBinding = egoProbeSerial_ == egoBoundSerial_;
        egoProbeSerial_.clear();
        if (!matchesCurrentBinding)
        {
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "ignore stale ego probe result after binding changed" << std::endl).str());
            return;
        }
        if (connected)
        {
            egoProbeFailureCount_ = 0;
            egoBoundConnected_ = true;
            clearEgoBindingFault();
        }
        else
        {
            ++egoProbeFailureCount_;
            if (egoProbeFailureCount_ >= kEgoProbeFailureThreshold)
            {
                egoBoundConnected_ = false;
                setEgoBindingFault("bound ego disconnected: " + egoBoundSerial_);
            }
        }
    }

    const uint64_t nowMs = currentSteadyMs();
    if (!egoProbeFuture_.valid() &&
        (egoLastProbeStartMs_ == 0 || nowMs - egoLastProbeStartMs_ >= kEgoProbeIntervalMs))
    {
        egoLastProbeStartMs_ = nowMs;
        egoProbeSerial_ = egoBoundSerial_;
        const std::vector<std::string> args = egoBindingWorkerArgs(
            options_.egoRecordingScript,
            "probe",
            options_.egoBindingFile,
            options_.egoBindingStatusFile);
        egoProbeFuture_ = std::async(std::launch::async, [args]() {
            return RecordRuntime::runCommandSync(args);
        });
    }
}

void RecordRuntime::setEgoBindingFault(const std::string &detail)
{
    if (egoBindingFaultActive_)
    {
        return;
    }
    egoBindingFaultActive_ = true;
    DM_LOG_ERROR_CODE(DA::Ugripper::UgrCodeForErrorType(
                          DA::Ugripper::kErrorTypeEgoDisconnected),
                      "{}", (::DA::utils::LogString() << detail << std::endl).str());
    if (isRecordingActive())
    {
        stopRecording(true, detail, DA::Ugripper::kErrorTypeEgoDisconnected);
    }
    setLedState(LedState::Error5);
    setAudioRecoveryCommand("error");
    sendAudioCommand("error");
}

void RecordRuntime::clearEgoBindingFault()
{
    if (!egoBindingFaultActive_)
    {
        return;
    }
    egoBindingFaultActive_ = false;
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "bound ego connection recovered serial=" << egoBoundSerial_ << std::endl).str());
    if (!isRecordingActive())
    {
        applyIdleState();
    }
}

bool RecordRuntime::waitForEgoFinalize(const std::string &episodeDir,
                                       int timeoutMs,
                                       std::string *errorMessage,
                                       std::string *errorType)
{
    if (errorType != nullptr)
    {
        errorType->clear();
    }
    if (!egoRecordingAttempted_)
    {
        if (isEgoRequired() && errorMessage != nullptr)
        {
            *errorMessage = "required ego recording was not attempted";
        }
        if (isEgoRequired() && errorType != nullptr)
        {
            *errorType = DA::Ugripper::kErrorTypeEgoPullIncomplete;
        }
        return !isEgoRequired();
    }
    const fs::path statusPath = fs::path(episodeDir) / "ego" / kEgoSyncStatusFileName;
    const uint64_t startMs = currentSteadyMs();
    while ((currentSteadyMs() - startMs) < static_cast<uint64_t>(std::max(timeoutMs, 0)))
    {
        json status = json::object();
        std::string loadError;
        if (loadJsonFile(statusPath.string(), &status, &loadError) && status.is_object())
        {
            const std::string state = status.value("status", std::string());
            if (state == "finalized")
            {
                return true;
            }
            if (state == "not_found" || state == "stop_no_remote_episode")
            {
                if (isEgoRequired())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = status.value("error", state);
                    }
                    if (errorType != nullptr)
                    {
                        *errorType = status.value(
                            "error_type",
                            state == "not_found"
                                ? std::string(DA::Ugripper::kErrorTypeEgoDisconnected)
                                : std::string(DA::Ugripper::kErrorTypeEgoPullIncomplete));
                    }
                    return false;
                }
                return true;
            }
            if (state == "device_disconnected" || state == "stop_failed" ||
                state == "pull_incomplete" || state == "stop_no_remote_root" ||
                state == "finalize_timeout" || state == "error")
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = status.value("error", state);
                }
                if (errorType != nullptr)
                {
                    const bool disconnected = state == "device_disconnected" || state == "stop_failed";
                    *errorType = status.value(
                        "error_type",
                        disconnected
                            ? std::string(DA::Ugripper::kErrorTypeEgoDisconnected)
                            : std::string(DA::Ugripper::kErrorTypeEgoPullIncomplete));
                }
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kEgoFinalizeWaitPollMs));
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = "timed out waiting for ego finalize status";
    }
    if (errorType != nullptr)
    {
        *errorType = DA::Ugripper::kErrorTypeEgoPullIncomplete;
    }
    return false;
}

bool RecordRuntime::mergeEpisodeInfo(const std::string &episodeDir, std::string *errorMessage) const
{
    const fs::path baseInfoPath = episodeTimingPath(episodeDir);

    std::ifstream baseInput(baseInfoPath);
    if (!baseInput.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string("cannot open internal timing shm file: ") + baseInfoPath.string() +
                            " (legacy label " + kEpisodeTimingFileName + ")";
        }
        return false;
    }

    json baseInfo;
    try
    {
        baseInfo = json::parse(baseInput);
    }
    catch (const std::exception &ex)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string("failed to parse internal timing shm file: ") +
                            baseInfoPath.string() + " error=" + ex.what();
        }
        return false;
    }

    auto timingErrorForCamera = [&baseInfo](const std::string &cameraName) {
        if (!baseInfo.contains("timing_errors") || !baseInfo["timing_errors"].is_array())
        {
            return std::string();
        }
        for (const auto &entry : baseInfo["timing_errors"])
        {
            if (!entry.is_object() || entry.value("camera_name", std::string()) != cameraName)
            {
                continue;
            }
            return entry.value("error", std::string());
        }
        return std::string();
    };
    for (const auto &artifact : activeEpisodeVideoArtifacts(chestCameraEnabled_, stereoEnabled_))
    {
        if (std::string(artifact.cameraName).find("stereo") != std::string::npos)
        {
            continue;
        }
        const std::string key = std::string(artifact.cameraName) + "_record_time_offset_us";
        if (!baseInfo.contains(key) || !baseInfo[key].is_number_integer())
        {
            if (errorMessage != nullptr)
            {
                std::string detail = timingErrorForCamera(artifact.cameraName);
                *errorMessage = "camera timing offset unavailable: " + std::string(artifact.cameraName);
                if (!detail.empty())
                {
                    *errorMessage += " error=" + detail;
                }
                if (baseInfo.contains(key) && baseInfo[key].is_null())
                {
                    *errorMessage += " value=null";
                }
            }
            return false;
        }
    }

    if (!stereoEnabled_)
    {
        return true;
    }

    for (const auto &target : kFaysTailCheckTargets)
    {
        FaysMcapSummary summary;
        std::string faysError;
        if (!loadFaysMcapSummary(
                (fs::path(episodeDir) / target.mcapFileName).string(),
                target.imuTopic,
                target.cameraTopic,
                &summary,
                &faysError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = std::string("failed to load Fays timing for side=") +
                                target.side + ": " + faysError;
            }
            return false;
        }

        const std::string prefix = std::string(target.side) + "_stereo";
        baseInfo[prefix + "_record_time_offset_us"] = summary.firstCameraPublishTimeNs / 1000ULL;
        baseInfo[prefix + "_effective_duration_us"] = summary.cameraPublishSpanNs / 1000ULL;
        baseInfo[prefix + "_camera_frame_count"] = summary.cameraMessageCount;
    }

    std::ofstream output(baseInfoPath, std::ios::trunc);
    if (!output.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = std::string("cannot rewrite internal timing shm file: ") + baseInfoPath.string() +
                            " (legacy label " + kEpisodeTimingFileName + ")";
        }
        return false;
    }
    output << baseInfo.dump(2) << '\n';
    return true;
}

bool RecordRuntime::syncRuntimeLogToDisk(const char *reason) const
{
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);

    // Persist system/journal logs alongside the runtime log at every stop boundary.
    const std::string systemLogCommand = "/usr/local/lib/ugripper/logging/save_system_log.sh " +
        shellQuote(options_.diskRoot + "/logs") + " " + shellQuote(toLower(deviceSn_.empty() ? "unknown_device" : deviceSn_));
    const auto systemLogStartMs = currentSteadyMs();
    const bool systemLogOk = std::system(systemLogCommand.c_str()) == 0;
    DM_LOG_INFO("[STOP_DIAG] system log snapshot finished: ok={} elapsed_ms={}",
                systemLogOk, currentSteadyMs() - systemLogStartMs);
    if (!systemLogOk) {
        DM_LOG_WARN("system log snapshot failed");
    }

    const std::string deviceSnLower = toLower(deviceSn_.empty() ? "unknown_device" : deviceSn_);
    const std::string filePrefix = "umi_sys_" + deviceSnLower + "_";
    const std::string reasonSuffix =
        (reason != nullptr && *reason != '\0') ? " after " + std::string(reason) : std::string();
    const fs::path localLogDir("/tmp");
    fs::path sourcePath;
    fs::file_time_type newestWriteTime{};
    bool foundSource = false;

    std::error_code iterError;
    for (fs::directory_iterator it(localLogDir, iterError), end; !iterError && it != end; it.increment(iterError))
    {
        const fs::directory_entry &entry = *it;
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::string fileName = entry.path().filename().string();
        if (fileName.size() <= filePrefix.size() + 4 ||
            fileName.compare(0, filePrefix.size(), filePrefix) != 0 ||
            fileName.compare(fileName.size() - 4, 4, ".log") != 0)
        {
            continue;
        }

        std::error_code timeError;
        const fs::file_time_type writeTime = entry.last_write_time(timeError);
        if (timeError)
        {
            continue;
        }

        if (!foundSource || writeTime > newestWriteTime)
        {
            sourcePath = entry.path();
            newestWriteTime = writeTime;
            foundSource = true;
        }
    }

    if (iterError)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to scan runtime log directory for sync" << reasonSuffix
                             << ": " << iterError.message()).str());
        return false;
    }

    if (!foundSource)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "no runtime log file found for sync" << reasonSuffix
                             << ": prefix=" << filePrefix).str());
        return false;
    }

    const fs::path diskRoot(options_.diskRoot);
    std::error_code diskRootError;
    if (!fs::exists(diskRoot, diskRootError) || !fs::is_directory(diskRoot, diskRootError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "skip runtime log sync" << reasonSuffix
                             << ": disk root unavailable: " << diskRoot).str());
        return false;
    }

    bool mounted = false;
    std::ifstream mountInfo("/proc/self/mountinfo");
    if (mountInfo.is_open())
    {
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
                mounted = true;
                break;
            }
        }
    }

    if (!mounted)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "skip runtime log sync" << reasonSuffix
                             << ": disk root is not mounted: " << diskRoot).str());
        return false;
    }

    if (access(diskRoot.c_str(), W_OK) != 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "skip runtime log sync" << reasonSuffix
                             << ": disk root not writable: " << diskRoot
                             << " error=" << std::strerror(errno)).str());
        return false;
    }

    const fs::path destDir = diskRoot / "logs";
    std::error_code mkdirError;
    fs::create_directories(destDir, mkdirError);
    if (mkdirError)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to create runtime log directory" << reasonSuffix
                             << ": " << destDir << " error=" << mkdirError.message()).str());
        return false;
    }

    const fs::path destPath = destDir / sourcePath.filename();
    const std::string sourceStem = sourcePath.stem().string();
    const fs::path statePath = fs::path("/tmp") / (sourceStem + ".pos");
    const fs::path lockPath = fs::path("/tmp") / (sourceStem + ".lock");

    const int lockFd = open(lockPath.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (lockFd < 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to open runtime log sync lock" << reasonSuffix
                             << ": " << lockPath << " error=" << std::strerror(errno)).str());
        return false;
    }

    const auto closeLock = [&]() {
        flock(lockFd, LOCK_UN);
        close(lockFd);
    };

    if (flock(lockFd, LOCK_EX) != 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to acquire runtime log sync lock" << reasonSuffix
                             << ": " << lockPath << " error=" << std::strerror(errno)).str());
        close(lockFd);
        return false;
    }

    int64_t lastPos = 0;
    {
        std::ifstream stateInput(statePath);
        if (stateInput.is_open())
        {
            stateInput >> lastPos;
            if (!stateInput.good() && !stateInput.eof())
            {
                lastPos = 0;
            }
        }
    }
    if (lastPos < 0)
    {
        lastPos = 0;
    }

    const int srcFd = open(sourcePath.c_str(), O_RDONLY | O_CLOEXEC);
    if (srcFd < 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to open runtime log source" << reasonSuffix
                             << ": " << sourcePath << " error=" << std::strerror(errno)).str());
        closeLock();
        return false;
    }

    struct stat sourceStat
    {
    };
    if (fstat(srcFd, &sourceStat) != 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to stat runtime log source" << reasonSuffix
                             << ": " << sourcePath << " error=" << std::strerror(errno)).str());
        close(srcFd);
        closeLock();
        return false;
    }

    int64_t currentSize = static_cast<int64_t>(sourceStat.st_size);
    if (currentSize < lastPos)
    {
        lastPos = 0;
    }

    if (currentSize <= lastPos)
    {
        close(srcFd);
        closeLock();
        DM_LOG_INFO("{}", (::DA::utils::LogString() << "runtime log already synced" << reasonSuffix
                             << ": " << destPath).str());
        return systemLogOk;
    }

    const int destFd = open(destPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (destFd < 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to open runtime log destination" << reasonSuffix
                             << ": " << destPath << " error=" << std::strerror(errno)).str());
        close(srcFd);
        closeLock();
        return false;
    }

    if (lseek(srcFd, static_cast<off_t>(lastPos), SEEK_SET) < 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to seek runtime log source" << reasonSuffix
                             << ": " << sourcePath << " error=" << std::strerror(errno)).str());
        close(destFd);
        close(srcFd);
        closeLock();
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    int64_t remaining = currentSize - lastPos;
    while (remaining > 0)
    {
        const size_t chunkSize = static_cast<size_t>(std::min<int64_t>(remaining, static_cast<int64_t>(buffer.size())));
        const ssize_t readSize = read(srcFd, buffer.data(), chunkSize);
        if (readSize < 0)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to read runtime log source" << reasonSuffix
                                 << ": " << sourcePath << " error=" << std::strerror(errno)).str());
            close(destFd);
            close(srcFd);
            closeLock();
            return false;
        }
        if (readSize == 0)
        {
            break;
        }

        ssize_t totalWritten = 0;
        while (totalWritten < readSize)
        {
            const ssize_t written = write(destFd, buffer.data() + totalWritten, static_cast<size_t>(readSize - totalWritten));
            if (written <= 0)
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to append runtime log destination" << reasonSuffix
                                     << ": " << destPath << " error=" << std::strerror(errno)).str());
                close(destFd);
                close(srcFd);
                closeLock();
                return false;
            }
            totalWritten += written;
        }

        remaining -= readSize;
    }

    if (::fsync(destFd) != 0)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush runtime log destination" << reasonSuffix
                             << ": " << destPath << " error=" << std::strerror(errno)).str());
        close(destFd);
        close(srcFd);
        closeLock();
        return false;
    }

    close(destFd);
    close(srcFd);

    std::string stateWriteError;
    if (!writeTextFileAtomically(statePath, std::to_string(currentSize), &stateWriteError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to update runtime log sync state" << reasonSuffix
                             << ": " << stateWriteError).str());
        closeLock();
        return false;
    }

    std::error_code flushError;
    bool directoriesFlushed = true;
    if (!flushDirectoryToDisk(destDir, &flushError))
    {
        directoriesFlushed = false;
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush runtime log directory" << reasonSuffix
                             << ": " << destDir << " error=" << flushError.message()).str());
    }
    if (!flushDirectoryToDisk(diskRoot, &flushError))
    {
        directoriesFlushed = false;
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush disk root after runtime log sync" << reasonSuffix
                             << ": " << diskRoot << " error=" << flushError.message()).str());
    }

    closeLock();

    DM_LOG_INFO("{}", (::DA::utils::LogString() << "synced runtime log to disk" << reasonSuffix
                         << ": src=" << sourcePath
                         << " dest=" << destPath
                         << " bytes=" << (currentSize - lastPos)).str());
    return systemLogOk && directoriesFlushed;
}

}  // namespace DA::Ugripper
