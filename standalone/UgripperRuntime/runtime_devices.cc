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
using namespace DA::Ugripper::detail;

namespace DA::Ugripper {

void RecordRuntime::handleGripperConnectionEvents()
{
    bool updatedAny = false;
    for (const auto &event : panelManager_.consumeConnectionEvents())
    {
        if (event.side.empty())
        {
            continue;
        }

        const size_t index = gripperStateIndexForSide(event.side);
        gripperFirmwareCache_[index] = {};
        nextFirmwareStatusMs_ = 0;
        auto &runtimeState = gripperRuntimeStates_[index];
        runtimeState.calibrationPayloadCached = false;
        runtimeState.calibrationPayload = {};
        runtimeState.calibrationReadAttempts = 0;
        runtimeState.nextCalibrationReadMs = 0;
        DA::Ugripper::GripperRefreshRuntimeView view{
            .side = runtimeState.side,
            .connected = runtimeState.connected,
            .last_error = runtimeState.lastError,
        };

        DA::Ugripper::ApplyGripperConnectionEvent(&view, &pendingGripperRefresh_[index], event.side, event.connected);
        runtimeState.side = view.side;
        runtimeState.connected = view.connected;
        runtimeState.lastError = view.last_error;

        if (event.connected)
        {
            noteRestoreUsbManualInsert(event.side, "hmi_reconnected");
            updatedAny = true;
            continue;
        }

        updatedAny = true;
    }

    if (updatedAny && episodeManager_ != nullptr)
    {
        episodeManager_->SetGripperRuntimeStates(gripperRuntimeStates_);
    }
}

void RecordRuntime::maintainGripperCalibration()
{
    if (isRecordingActive()) return;
    bool changed = false;
    for (auto &state : gripperRuntimeStates_)
    {
        if (!state.connected || state.calibrationPayloadCached ||
            state.calibrationReadAttempts >= 3 ||
            currentSteadyMs() < state.nextCalibrationReadMs ||
            !panelManager_.isSideReadyForRefresh(state.side, kGripperRefreshActiveTimeoutMs))
            continue;
        ++state.calibrationReadAttempts;
        state.nextCalibrationReadMs = currentSteadyMs() + 30000;
        state.calibrationPayloadCached =
            panelManager_.readCalibrationForSide(state.side, &state.calibrationPayload);
        DM_LOG_INFO("gripper calibration cache: side={} read={} attempt={}",
                    state.side, state.calibrationPayloadCached, state.calibrationReadAttempts);
        changed = true;
    }
    if (changed && episodeManager_ != nullptr)
        episodeManager_->SetGripperRuntimeStates(gripperRuntimeStates_);
}

void RecordRuntime::maintainFirmwareStatus()
{
    const uint64_t now = currentSteadyMs();
    if (now < nextFirmwareStatusMs_)
    {
        return;
    }
    json snapshot = {{"main_camera", json::object()}, {"gripper", json::object()}};
    for (const std::string side : {"left", "right"})
    {
        auto &cache = gripperFirmwareCache_[gripperStateIndexForSide(side)];
        const bool connected = panelManager_.isSideReadyForRefresh(side, kGripperRefreshActiveTimeoutMs);
        if (!fs::exists("/dev/" + side + "_gripper"))
        {
            cache = {};
        }
        // Serial ownership stays with the HMI driver. Never pause its I/O for
        // version queries during recording; each query is bounded and retries are rate limited.
        if (connected && !isRecordingActive() && cache.version.empty() &&
            now >= cache.nextAttemptMs)
        {
            ++cache.attempts;
            cache.nextAttemptMs = now + 30000;
            panelManager_.readFirmwareVersionForSide(side, &cache.version);
            cache.checkedAtMs = currentEpochUs() / 1000;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "gripper firmware cache: side=" << side
                << " version=" << (cache.version.empty() ? "unknown" : cache.version)
                << " attempt=" << cache.attempts).str());
        }
        snapshot["gripper"][side] = {
            {"version", connected ? cache.version : ""},
            {"serial", connected ? gripperRuntimeStates_[gripperStateIndexForSide(side)].serialNumber : ""},
            {"connected", connected},
            {"checked_at", cache.checkedAtMs / 1000.0},
            {"status", !connected ? "disconnected" : !cache.version.empty() ? "ready" : "pending"},
        };

        std::string iserial;
        std::error_code error;
        const auto device = fs::canonical("/dev/cam_" + side, error);
        if (!error)
        {
            auto node = fs::canonical(fs::path("/sys/class/video4linux") / device.filename(), error);
            while (!error && !node.empty() && node != node.root_path())
            {
                if (fs::exists(node / "idVendor", error) && fs::exists(node / "serial", error))
                {
                    std::ifstream input(node / "serial");
                    std::getline(input, iserial);
                    break;
                }
                node = node.parent_path();
            }
        }
        snapshot["main_camera"][side] = {{"iserial", iserial}};
    }
    snapshot["updated_at_ms"] = currentEpochUs() / 1000;
    snapshot["pid"] = getpid();
    std::string error;
    if (!writeTextFileAtomically(options_.firmwareStatusFile, snapshot.dump() + "\n", &error))
    {
        DM_LOG_WARN("firmware status publish failed: {}", error);
    }
    nextFirmwareStatusMs_ = currentSteadyMs() + 1000;
}

void RecordRuntime::processPendingGripperRefreshes()
{
    bool updatedAny = false;
    for (const std::string side : {"left", "right"})
    {
        const size_t index = gripperStateIndexForSide(side);
        if (!pendingGripperRefresh_[index])
        {
            continue;
        }

        auto &state = gripperRuntimeStates_[index];
        DA::Ugripper::GripperRefreshRuntimeView view{
            .side = state.side,
            .connected = state.connected,
            .last_error = state.lastError,
        };
        const auto action = DA::Ugripper::AdvancePendingGripperRefresh(
            &view,
            &pendingGripperRefresh_[index],
            side,
            areSideCriticalDevicesReady(side),
            panelManager_.isSideReadyForRefresh(side, kGripperRefreshActiveTimeoutMs));
        state.side = view.side;
        state.connected = view.connected;
        state.lastError = view.last_error;

        if (action == DA::Ugripper::GripperRefreshAction::RequestState)
        {
            panelManager_.requestStateForSide(side);
            updatedAny = true;
            continue;
        }

        if (action == DA::Ugripper::GripperRefreshAction::RefreshRuntimeState)
        {
            refreshGripperRuntimeStateForSide(side, true);
        }
        updatedAny = true;
    }

    if (updatedAny && episodeManager_ != nullptr)
    {
        episodeManager_->SetGripperRuntimeStates(gripperRuntimeStates_);
    }
}

void RecordRuntime::clearGripperRuntimeStateForSide(const std::string &side,
                                                    bool connected,
                                                    const std::string &status,
                                                    const std::string &errorMessage)
{
    auto &state = gripperRuntimeStates_[gripperStateIndexForSide(side)];
    state.side = side;
    state.connected = connected;
    state.hasSerialNumber = false;
    state.serialNumber.clear();
    state.lastError = errorMessage;
}

void RecordRuntime::refreshGripperRuntimeStateForSide(const std::string &side,
                                                      bool markTactileReferencePending)
{
    auto &state = gripperRuntimeStates_[gripperStateIndexForSide(side)];
    clearGripperRuntimeStateForSide(side, true, "refreshing", "");

    std::string serialNumber;
    bool hasSerialNumber = false;
    std::string runtimeReadError;
    if (!panelManager_.readRuntimeIdentityForSide(side, &serialNumber, &hasSerialNumber, &runtimeReadError))
    {
        if (!runtimeReadError.empty())
        {
            state.lastError = runtimeReadError;
        }
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "gripper runtime cache refresh failed: side=" << side
            << " detail=" << (runtimeReadError.empty() ? "readRuntimeIdentityForSide failed" : runtimeReadError)
            << std::endl).str());
        return;
    }

    state.connected = true;
    state.hasSerialNumber = hasSerialNumber;
    state.serialNumber = hasSerialNumber ? serialNumber : "";
    state.lastError.clear();
    DM_LOG_INFO("{}", (::DA::utils::LogString()
        << "gripper runtime cache refreshed: side=" << side
        << " has_sn=" << boolText(hasSerialNumber)
        << " sn=" << state.serialNumber
        << std::endl).str());
    if (DA::Ugripper::ShouldMarkTactileReferencePending(markTactileReferencePending))
    {
        markTactileReferencePendingForSide(side, "gripper_reconnected");
    }
}

void RecordRuntime::initializeMainCameraRuntimeStates()
{
    mainCameraRuntimeStates_.clear();
    const std::array<std::tuple<const char *, const char *, bool>, 3> cameras = {{
        {"right_cam_main", "/dev/cam_right", true},
        {"left_cam_main", "/dev/cam_left", true},
        {"chest_cam_main", "/dev/cam_chest", chestCameraEnabled_},
    }};

    const uint64_t nowMs = currentSteadyMs();
    for (const auto &[cameraName, devicePath, enabled] : cameras)
    {
        MainCameraRuntimeCache state;
        state.cameraName = cameraName;
        state.devicePath = devicePath;
        state.enabled = enabled;
        state.nextRefreshAllowedMs = nowMs + kMainCameraRefreshDelayMs;
        mainCameraRuntimeStates_.push_back(std::move(state));
    }
    syncMainCameraRuntimeStatesToEpisodeManager();
}

void RecordRuntime::maintainMainCameraRuntimeStates()
{
    bool updatedAny = false;
    const uint64_t nowMs = currentSteadyMs();

    for (auto &state : mainCameraRuntimeStates_)
    {
        if (!state.enabled)
        {
            continue;
        }

        if (state.refreshFuture.valid() &&
            state.refreshFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            const auto result = state.refreshFuture.get();
            if (state.present && result.resolvedTarget == state.resolvedTarget && result.errorMessage.empty())
            {
                state.serialNumber = result.serialNumber;
                state.calibrationPayloadCached = result.calibrationPayloadCached;
                state.calibrationPayload = result.calibrationPayload;
                state.lastError.clear();
                state.nextRefreshAllowedMs = std::numeric_limits<uint64_t>::max();
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "main camera runtime cache refreshed: camera=" << state.cameraName
                    << " device=" << state.devicePath
                    << " target=" << state.resolvedTarget
                    << " sn=" << state.serialNumber << std::endl).str());
            }
            else if (state.present && result.resolvedTarget == state.resolvedTarget)
            {
                const bool shouldLogFailure =
                    state.lastError.empty() ||
                    state.lastError == "waiting for XU cache refresh" ||
                    state.lastError == "refreshing";
                state.serialNumber.clear();
                state.calibrationPayloadCached = false;
                state.calibrationPayload = {};
                state.lastError = result.errorMessage.empty() ? "unknown XU read failure" : result.errorMessage;
                state.nextRefreshAllowedMs = nowMs + kMainCameraRefreshRetryMs;
                if (shouldLogFailure)
                {
                    DM_LOG_WARN("{}", (::DA::utils::LogString()
                        << "main camera runtime cache refresh failed: camera=" << state.cameraName
                        << " device=" << state.devicePath
                        << " target=" << state.resolvedTarget
                        << " detail=" << state.lastError << std::endl).str());
                }
            }
            updatedAny = true;
        }

        const bool exists = fs::exists(state.devicePath);
        if (!exists)
        {
            if (state.present || !state.serialNumber.empty() || state.calibrationPayloadCached)
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "main camera device removed, clear runtime cache: camera=" << state.cameraName
                    << " device=" << state.devicePath << std::endl).str());
                state.present = false;
                state.resolvedTarget.clear();
                state.serialNumber.clear();
                state.calibrationPayloadCached = false;
                state.calibrationPayload = {};
                state.lastError = "device node missing";
                updatedAny = true;
            }
            continue;
        }

        const std::string resolvedTarget = resolveDeviceNodeTarget(state.devicePath);
        if (!state.present || state.resolvedTarget != resolvedTarget)
        {
            state.present = true;
            state.resolvedTarget = resolvedTarget;
            state.serialNumber.clear();
            state.calibrationPayloadCached = false;
            state.calibrationPayload = {};
            state.lastError = "waiting for XU cache refresh";
            state.nextRefreshAllowedMs = nowMs + kMainCameraRefreshDelayMs;
            updatedAny = true;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "main camera device detected, schedule runtime cache refresh: camera=" << state.cameraName
                << " device=" << state.devicePath
                << " target=" << state.resolvedTarget << std::endl).str());
        }

        if (state.refreshFuture.valid() || nowMs < state.nextRefreshAllowedMs)
        {
            continue;
        }

        const std::string cameraName = state.cameraName;
        const std::string devicePath = state.devicePath;
        const std::string resolvedTargetForRead = state.resolvedTarget;
        if (state.lastError.empty() || state.lastError == "waiting for XU cache refresh")
        {
            state.lastError = "refreshing";
        }
        state.refreshFuture = std::async(std::launch::async,
            [cameraName, devicePath, resolvedTargetForRead]() {
                MainCameraRefreshResult result;
                result.cameraName = cameraName;
                result.devicePath = devicePath;
                result.resolvedTarget = resolvedTargetForRead;
                std::string detail;
                const auto identity =
                    readYuzhouMainCameraIdentity(cameraName, devicePath, resolvedTargetForRead, &detail);
                if (!identity.has_value())
                {
                    result.errorMessage = detail.empty() ? "failed to read Yuzhou XU identity" : detail;
                    return result;
                }
                result.serialNumber = identity->serialNumber;
                result.calibrationPayloadCached = identity->calibrationPayloadCached;
                result.calibrationPayload = identity->calibrationPayload;
                return result;
            });
        updatedAny = true;
    }

    if (updatedAny)
    {
        syncMainCameraRuntimeStatesToEpisodeManager();
    }
}

void RecordRuntime::syncMainCameraRuntimeStatesToEpisodeManager()
{
    if (episodeManager_ == nullptr)
    {
        return;
    }

    std::array<EpisodeManager::MainCameraRuntimeState, 3> states{};
    for (size_t index = 0; index < states.size() && index < mainCameraRuntimeStates_.size(); ++index)
    {
        const auto &source = mainCameraRuntimeStates_[index];
        auto &target = states[index];
        target.cameraName = source.cameraName;
        target.devicePath = source.devicePath;
        target.enabled = source.enabled;
        target.present = source.present;
        target.serialNumber = source.serialNumber;
        target.calibrationPayloadCached = source.calibrationPayloadCached;
        target.calibrationPayload = source.calibrationPayload;
        target.lastError = source.lastError;
    }
    episodeManager_->SetMainCameraRuntimeStates(states);
}

void RecordRuntime::initializeTactileCameraRuntimeStates()
{
    tactileCameraRuntimeStates_.clear();
    const uint64_t nowMs = currentSteadyMs();
    for (const auto &target : kTactileCalibrationTargets)
    {
        TactileCameraRuntimeCache state;
        state.cameraName = target.cameraName;
        state.side = target.side;
        state.devicePath = target.devicePath;
        state.nextRefreshAllowedMs = nowMs;
        tactileCameraRuntimeStates_.push_back(std::move(state));
    }
    syncTactileCameraRuntimeStatesToEpisodeManager();
}

void RecordRuntime::maintainTactileCameraRuntimeStates()
{
    bool updatedAny = false;
    const uint64_t nowMs = currentSteadyMs();

    for (auto &state : tactileCameraRuntimeStates_)
    {
        const bool exists = fs::exists(state.devicePath);
        if (!exists)
        {
            if (state.present || !state.serialNumber.empty())
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile camera device removed, clear runtime serial cache: camera=" << state.cameraName
                    << " device=" << state.devicePath << std::endl).str());
                state.present = false;
                state.resolvedTarget.clear();
                state.serialNumber.clear();
                state.lastError = "device node missing";
                updatedAny = true;
            }
            continue;
        }

        const std::string resolvedTarget = resolveDeviceNodeTarget(state.devicePath);
        if (!state.present || state.resolvedTarget != resolvedTarget)
        {
            state.present = true;
            state.resolvedTarget = resolvedTarget;
            state.serialNumber.clear();
            state.lastError = "waiting for tactile serial cache refresh";
            state.nextRefreshAllowedMs = nowMs;
            updatedAny = true;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "tactile camera device detected, schedule runtime serial cache refresh: camera=" << state.cameraName
                << " device=" << state.devicePath
                << " target=" << state.resolvedTarget << std::endl).str());
        }

        if (nowMs < state.nextRefreshAllowedMs)
        {
            continue;
        }

        std::string detail;
        const auto serial = readTactileUsbSerialFromSysfs(state.devicePath, state.resolvedTarget, &detail);
        if (serial.has_value() && !serial->empty())
        {
            state.serialNumber = *serial;
            state.lastError.clear();
            state.nextRefreshAllowedMs = std::numeric_limits<uint64_t>::max();
            updatedAny = true;
            DM_LOG_INFO("{}", (::DA::utils::LogString()
                << "tactile camera runtime serial cache refreshed: camera=" << state.cameraName
                << " device=" << state.devicePath
                << " target=" << state.resolvedTarget
                << " serial=" << state.serialNumber << std::endl).str());
            continue;
        }

        const std::string newError = detail.empty() ? "failed to read tactile USB serial from sysfs" : detail;
        const bool shouldLogFailure =
            state.lastError.empty() ||
            state.lastError == "waiting for tactile serial cache refresh" ||
            state.lastError != newError;
        state.serialNumber.clear();
        state.lastError = newError;
        state.nextRefreshAllowedMs = nowMs + kTactileSerialRefreshRetryMs;
        updatedAny = true;
        if (shouldLogFailure)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "tactile camera runtime serial cache refresh failed: camera=" << state.cameraName
                << " device=" << state.devicePath
                << " target=" << state.resolvedTarget
                << " detail=" << state.lastError << std::endl).str());
        }
    }

    if (updatedAny)
    {
        syncTactileCameraRuntimeStatesToEpisodeManager();
    }
}

void RecordRuntime::syncTactileCameraRuntimeStatesToEpisodeManager()
{
    if (episodeManager_ == nullptr)
    {
        return;
    }

    std::array<EpisodeManager::TactileCameraRuntimeState, 4> states{};
    for (size_t index = 0; index < states.size() && index < tactileCameraRuntimeStates_.size(); ++index)
    {
        const auto &source = tactileCameraRuntimeStates_[index];
        auto &target = states[index];
        target.cameraName = source.cameraName;
        target.side = source.side;
        target.devicePath = source.devicePath;
        target.present = source.present;
        target.serialNumber = source.serialNumber;
        target.lastError = source.lastError;
    }
    episodeManager_->SetTactileCameraRuntimeStates(states);
}

void RecordRuntime::waitForMainCameraRefreshes()
{
    for (auto &state : mainCameraRuntimeStates_)
    {
        if (state.refreshFuture.valid())
        {
            state.refreshFuture.wait();
        }
    }
}

void RecordRuntime::markTactileReferencePendingForSide(const std::string &side,
                                                       const std::string &reason)
{
    if (episodeManager_ == nullptr)
    {
        return;
    }
    episodeManager_->MarkTactileReferencePendingForSide(side, reason);
}

void RecordRuntime::applyTactileValidationFindings(
    const std::vector<EpisodeManager::TactileValidationFinding> &findings)
{
    bool warningActive = false;
    if (egoFeedback_ && std::any_of(findings.begin(), findings.end(),
            [](const auto& finding) { return finding.warningTriggered; })) {
        egoFeedback_->Notify(1205);
    }
    tactileWarningSides_ = {};
    for (const auto &finding : findings)
    {
        if (tactileTriggeredAudioCommand_.empty() && finding.warningTriggered)
        {
            tactileTriggeredAudioCommand_ = finding.audioCommand;
        }
        if (finding.warningActive)
        {
            warningActive = true;
            TactileWarningSideState *sideState = nullptr;
            if (finding.side == "left")
            {
                sideState = &tactileWarningSides_[0];
            }
            else if (finding.side == "right")
            {
                sideState = &tactileWarningSides_[1];
            }
            if (sideState != nullptr)
            {
                if (finding.sensorSlot == "l")
                {
                    sideState->leftSensor = true;
                }
                else if (finding.sensorSlot == "r")
                {
                    sideState->rightSensor = true;
                }
            }
        }
    }
    tactileWarningActive_ = warningActive;
}

void RecordRuntime::scheduleBackgroundTactileValidation(const std::string &episodeDir)
{
    if (episodeDir.empty())
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(backgroundTactileMutex_);
        pendingBackgroundTactileEpisodeDir_ = episodeDir;
    }
    logPerf((::DA::utils::LogString()
             << "[PERF] tactile background validation queued: episode_dir=" << episodeDir).str());
}

void RecordRuntime::maintainTactileBaselineReset()
{
    const fs::path root(options_.tactileStateDir);
    const fs::path requestPath = root / "web_reset_request.json";
    if (isRecordingActive() || backgroundTactileFuture_.valid() || !fs::exists(requestPath)) return;
    json response;
    std::map<fs::path, std::optional<std::string>> originals;
    try {
        json request;
        std::ifstream(requestPath) >> request;
        response["id"] = request.at("id");
        const std::string name = request.at("camera");
        const std::string serial = request.at("serial");
        const std::string scope = request.at("scope");
        if (scope != "reference" && scope != "persistent" && scope != "both")
            throw std::runtime_error("invalid baseline scope");
        const auto target = std::find_if(kTactileCalibrationTargets.begin(), kTactileCalibrationTargets.end(),
            [&](const auto& item) { return name == item.cameraName; });
        if (target == kTactileCalibrationTargets.end()) throw std::runtime_error("unknown tactile camera");
        const auto device = std::find_if(tactileCameraRuntimeStates_.begin(), tactileCameraRuntimeStates_.end(),
            [&](const auto& item) { return item.cameraName == name && item.serialNumber == serial && item.present; });
        if (serial.empty() || device == tactileCameraRuntimeStates_.end())
            throw std::runtime_error("sensor identity changed; capture again");
        const uint64_t requested = request.at("requested_at_ms");
        const uint64_t now = currentEpochMs();
        if (requested > now || now - requested > 15000) throw std::runtime_error("reset request expired");
        std::vector<uint8_t> frame;
        std::string error;
        if (!readBinaryFileExact(root / "web_reset.gray", kTactileFrameBytes, &frame, &error))
            throw std::runtime_error(error);
        json history = json::object();
        std::ifstream input(tactileHistoryPath(root));
        if (input.is_open()) input >> history;
        if (!history.contains("cameras")) history["cameras"] = json::object();
        // Keep rollback contents until the files and history have all committed.
        std::vector<fs::path> changedPaths{tactileHistoryPath(root)};
        if (scope == "reference" || scope == "both") {
            changedPaths.push_back(tactileReferenceRawPath(root, serial));
            changedPaths.push_back(tactileReferenceRawPath(root, serial).replace_extension(".json"));
        }
        if (scope == "persistent" || scope == "both") changedPaths.push_back(tactilePersistentRawPath(root, serial));
        for (const auto& path : changedPaths) {
            if (fs::exists(path)) {
                std::ifstream old(path, std::ios::binary);
                if (!old) throw std::runtime_error("cannot back up baseline");
                originals[path] = std::string(std::istreambuf_iterator<char>(old), {});
            } else originals[path] = std::nullopt;
        }
        auto& entry = history["cameras"][serial];
        if (!entry.is_object()) entry = json::object();
        entry["camera_name"] = name;
        entry["updated_at_ms"] = now;
        auto saveFrame = [&](const fs::path& path) {
            fs::create_directories(path.parent_path());
            const fs::path temporary = path.string() + ".web-new";
            if (!writeBinaryFile(temporary, frame, &error)) throw std::runtime_error(error);
            fs::rename(temporary, path);
        };
        if (scope == "reference" || scope == "both") {
            saveFrame(tactileReferenceRawPath(root, serial));
            if (!writeTactileReferenceMeta(root, serial, *target, now, &error)) throw std::runtime_error(error);
            entry["recent"] = json::array();
            entry["reference_pending"] = false;
            entry["reference_pending_reason"] = "";
            entry["reference_updated_at_ms"] = now;
        }
        if (scope == "persistent" || scope == "both") {
            saveFrame(tactilePersistentRawPath(root, serial));
            entry["persistent_recent"] = json::array();
            entry["persistent_fault_active"] = false;
            entry["persistent_fault_detail"] = "";
            entry["persistent_baseline_pending"] = false;
            entry["persistent_baseline_updated_at_ms"] = now;
            entry["persistent_baseline_refresh_on_next_boot"] = false;
            entry["persistent_baseline_refresh_request_boot_id"] = "";
        }
        if (!writeTextFileAtomically(tactileHistoryPath(root), history.dump(2), &error))
            throw std::runtime_error(error);
        std::vector<EpisodeManager::TactileValidationFinding> findings;
        for (const auto& item : kTactileCalibrationTargets) {
            const auto cached = std::find_if(tactileCameraRuntimeStates_.begin(), tactileCameraRuntimeStates_.end(),
                [&](const auto& state) { return state.cameraName == item.cameraName; });
            if (cached == tactileCameraRuntimeStates_.end()) continue;
            const auto saved = history["cameras"].value(cached->serialNumber, json::object());
            const auto recent = saved.value("recent", json::array());
            EpisodeManager::TactileValidationFinding finding;
            finding.cameraName = item.cameraName;
            finding.side = item.side;
            finding.sensorSlot = item.sensorSlot;
            finding.warningActive = saved.value("persistent_fault_active", false) ||
                (recent.size() >= 3 && std::all_of(recent.begin(), recent.end(),
                    [](const auto& value) { return value.is_boolean() && value == true; }));
            findings.push_back(finding);
        }
        {
            std::lock_guard<std::mutex> lock(backgroundTactileMutex_);
            pendingBackgroundTactileEpisodeDir_.clear();
        }
        tactileTriggeredAudioCommand_.clear();
        applyTactileValidationFindings(findings);
        applyIdleState();
        response["status"] = "ok";
    } catch (const std::exception& ex) {
        response["status"] = "failed";
        response["error"] = ex.what();
        for (const auto& saved : originals) {
            std::string rollbackError;
            std::error_code ec;
            if (saved.second.has_value()) {
                if (!writeTextFileAtomically(saved.first, *saved.second, &rollbackError))
                    response["rollback_error"] = rollbackError;
            } else fs::remove(saved.first, ec);
            fs::remove(saved.first.string() + ".web-new", ec);
        }
    }
    std::string error;
    writeTextFileAtomically(root / "web_reset_response.json", response.dump(), &error);
    std::error_code ec;
    fs::remove(requestPath, ec);
    fs::remove(root / "web_reset.gray", ec);
}

void RecordRuntime::maintainBackgroundTactileValidation()
{
    if (backgroundTactileFuture_.valid() &&
        backgroundTactileFuture_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        BackgroundTactileValidationResult result = backgroundTactileFuture_.get();
        if (!result.findings.empty())
        {
            applyTactileValidationFindings(result.findings);
            if (!isRecordingActive())
            {
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
        }
        logPerf((::DA::utils::LogString()
                 << "[PERF] tactile background validation applied: episode_dir="
                 << result.episodeDir
                 << " findings=" << result.findings.size()).str());
    }

    if (backgroundTactileFuture_.valid() || isRecordingActive() || episodeManager_ == nullptr)
    {
        return;
    }

    maintainTactileBaselineReset();

    std::string episodeDir;
    {
        std::lock_guard<std::mutex> lock(backgroundTactileMutex_);
        episodeDir.swap(pendingBackgroundTactileEpisodeDir_);
    }
    if (episodeDir.empty())
    {
        return;
    }

    stopBackgroundTactileValidation_.store(false);
    logPerf((::DA::utils::LogString()
             << "[PERF] tactile background validation start: episode_dir=" << episodeDir).str());
    EpisodeManager *manager = episodeManager_.get();
    std::atomic<bool> *stopFlag = &stopBackgroundTactileValidation_;
    backgroundTactileFuture_ = std::async(
        std::launch::async,
        [manager, episodeDir, stopFlag]() {
            BackgroundTactileValidationResult result;
            result.episodeDir = episodeDir;
            if (stopFlag->load())
            {
                logPerf((::DA::utils::LogString()
                         << "[PERF] tactile background validation skipped: episode_dir="
                         << episodeDir
                         << " reason=stop_requested").str());
                return result;
            }
            manager->ValidateTactileEpisode(episodeDir, &result.findings);
            if (stopFlag->load())
            {
                result.findings.clear();
                logPerf((::DA::utils::LogString()
                         << "[PERF] tactile background validation discarded: episode_dir="
                         << episodeDir
                         << " reason=stop_requested").str());
            }
            return result;
        });
}

void RecordRuntime::requestStopBackgroundTactileValidation()
{
    stopBackgroundTactileValidation_.store(true);
    {
        std::lock_guard<std::mutex> lock(backgroundTactileMutex_);
        pendingBackgroundTactileEpisodeDir_.clear();
    }
    if (backgroundTactileFuture_.valid())
    {
        logPerf("[PERF] tactile background validation wait for stop");
        backgroundTactileFuture_.wait();
        backgroundTactileFuture_.get();
    }
}

void RecordRuntime::cancelBackgroundTactileValidation()
{
    stopBackgroundTactileValidation_.store(true);
    {
        std::lock_guard<std::mutex> lock(backgroundTactileMutex_);
        pendingBackgroundTactileEpisodeDir_.clear();
    }
    if (backgroundTactileFuture_.valid())
    {
        logPerf("[PERF] tactile background validation cancel requested");
    }
}

void RecordRuntime::applyIdleState()
{
    resetRestoreUsbErrorWindow();
    if (!episodeManagerInitialized_)
    {
        setLedState(LedState::WaitStorage);
        return;
    }
    if (egoBindingFaultActive_)
    {
        setLedState(LedState::Error5);
        return;
    }
    const std::optional<DA::Ugripper::HealthFault> fault = currentHealthFault();
    if (fault.has_value())
    {
        if (isStereoStartupWaiting())
        {
            setLedState(LedState::Init);
        }
        else
        {
            setHardwareFaultLedState(*fault);
        }
        return;
    }
    if (isEgoSyncPending())
    {
        setLedState(LedState::Warning);
        return;
    }
    if (tactileWarningActive_)
    {
        setTactileWarningLedState();
        return;
    }
    setLedState(LedState::Ready);
}

bool RecordRuntime::areSideCriticalDevicesReady(const std::string &side) const
{
    const auto &paths = side == "left" ? kLeftCriticalDevicePaths : kRightCriticalDevicePaths;
    for (const char *path : paths)
    {
        if (!stereoEnabled_ && isStereoDevicePath(path))
        {
            continue;
        }
        std::error_code error;
        if (!fs::exists(path, error))
        {
            return false;
        }
    }
    return true;
}


bool RecordRuntime::GripperPanelManager::connect(const std::vector<std::string> &ports)
{
    disconnect();
    drivers_.clear();
    reconnectAttemptMs_.clear();
    delayedStateRequestDueMs_.clear();
    lastKnownConnectedStates_.clear();
    pendingConnectionEvents_.clear();
    currentDriverBeepStates_.clear();
    inputDriverIndex_ = 0;
    hasDedicatedRightInput_ = false;
    hasLedEffect_ = false;
    hasDirectLedColor_ = false;

    for (size_t index = 0; index < ports.size(); ++index)
    {
        auto driver = std::make_unique<GripperHmiDriver>(ports[index], 115200, "RecordRuntimeHmi" + std::to_string(index));
        if (!hasDedicatedRightInput_ && ports[index].find("right_gripper") != std::string::npos)
        {
            inputDriverIndex_ = index;
            hasDedicatedRightInput_ = true;
        }
        if (!driver->connect())
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to connect HMI port: " << ports[index] << std::endl).str());
        }
        drivers_.push_back(std::move(driver));
        reconnectAttemptMs_.push_back(0);
        delayedStateRequestDueMs_.push_back(0);
        lastKnownConnectedStates_.push_back(false);
        currentDriverBeepStates_.push_back({});
        recordConnectionEventIfChanged(index, drivers_.back() != nullptr && drivers_.back()->isConnected());
    }

    return hasConnectedDevice();
}

void RecordRuntime::GripperPanelManager::disconnect()
{
    drivers_.clear();
    reconnectAttemptMs_.clear();
    delayedStateRequestDueMs_.clear();
    lastKnownConnectedStates_.clear();
    pendingConnectionEvents_.clear();
    currentDriverBeepStates_.clear();
}

bool RecordRuntime::GripperPanelManager::hasConnectedDevice() const
{
    for (const auto &driver : drivers_)
    {
        if (driver != nullptr && driver->isConnected())
        {
            return true;
        }
    }
    return false;
}

RecordRuntime::GripperPanelManager::HealthSnapshot RecordRuntime::GripperPanelManager::getHealthSnapshot(uint64_t activeTimeoutMs) const
{
    HealthSnapshot health;

    for (size_t index = 0; index < drivers_.size(); ++index)
    {
        const auto &driver = drivers_[index];
        if (driver == nullptr)
        {
            continue;
        }

        const bool connected = driver->isConnected();
        if (connected)
        {
            health.hasConnectedDevice = true;
        }
        else
        {
            health.disconnectedPorts.push_back(driver->getPort());
        }

        if (index == inputDriverIndex_)
        {
            health.inputConnected = connected;
        }

        if (!connected)
        {
            continue;
        }

        const auto snapshot = driver->getSnapshot(activeTimeoutMs);
        const std::string port_activity =
            driver->getPort() + "(age_ms=" + std::to_string(snapshot.lastRxAgeMs) + ",active=" +
            (snapshot.active ? "1" : "0") + ")";
        health.portActivity.push_back(port_activity);
        if (!snapshot.active)
        {
            health.inactivePorts.push_back(driver->getPort());
            health.inactivePortDetails.push_back(port_activity);
        }
        if (index == inputDriverIndex_)
        {
            health.inputActive = snapshot.active;
            health.inputLastRxAgeMs = snapshot.lastRxAgeMs;
        }
    }

    return health;
}

std::string RecordRuntime::GripperPanelManager::sideForPort(const std::string &port)
{
    if (port.find("right_gripper") != std::string::npos)
    {
        return "right";
    }
    if (port.find("left_gripper") != std::string::npos)
    {
        return "left";
    }
    return "";
}

int RecordRuntime::GripperPanelManager::findDriverIndexForSide(const std::string &side) const
{
    for (size_t index = 0; index < drivers_.size(); ++index)
    {
        const auto &driver = drivers_[index];
        if (driver != nullptr && sideForPort(driver->getPort()) == side)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void RecordRuntime::GripperPanelManager::recordConnectionEventIfChanged(size_t index, bool connected)
{
    if (index >= drivers_.size() || index >= lastKnownConnectedStates_.size() || drivers_[index] == nullptr)
    {
        return;
    }
    if (lastKnownConnectedStates_[index] == connected)
    {
        return;
    }

    lastKnownConnectedStates_[index] = connected;
    const std::string side = sideForPort(drivers_[index]->getPort());
    if (side.empty())
    {
        return;
    }
    pendingConnectionEvents_.push_back(ConnectionEvent{side, connected});
}

void RecordRuntime::GripperPanelManager::maybeReconnectDriver(size_t index)
{
    if (index >= drivers_.size() || drivers_[index] == nullptr)
    {
        return;
    }
    if (drivers_[index]->isConnected())
    {
        return;
    }

    const uint64_t nowMs = RecordRuntime::currentSteadyMs();
    if (index < reconnectAttemptMs_.size() &&
        reconnectAttemptMs_[index] != 0 &&
        (nowMs - reconnectAttemptMs_[index]) < kReconnectIntervalMs)
    {
        return;
    }

    if (index < reconnectAttemptMs_.size())
    {
        reconnectAttemptMs_[index] = nowMs;
    }

    if (!drivers_[index]->connect())
    {
        return;
    }

    DM_LOG_INFO("{}", (::DA::utils::LogString() << "reconnected HMI port: " << drivers_[index]->getPort() << std::endl).str());
    recordConnectionEventIfChanged(index, true);
    if (index < delayedStateRequestDueMs_.size())
    {
        delayedStateRequestDueMs_[index] = nowMs + 1000;
    }
    if (index < currentDriverBeepStates_.size())
    {
        const auto &beepState = currentDriverBeepStates_[index];
        if (beepState.duty == 0)
        {
            drivers_[index]->silenceBeep();
        }
        else
        {
            drivers_[index]->setBeepState(beepState);
        }
    }
    if (hasDirectLedColor_)
    {
        drivers_[index]->setLedColor(currentLedColor_);
    }
    else if (hasLedEffect_)
    {
        drivers_[index]->setLedEffect(currentLedEffect_);
    }
    if (index < perSideLedStates_.size())
    {
        const auto &sideLed = perSideLedStates_[index];
        if (sideLed.hasColor)
        {
            drivers_[index]->setLedColor(sideLed.color);
        }
        else if (sideLed.hasEffect)
        {
            drivers_[index]->setLedEffect(sideLed.effect);
        }
    }
}

bool RecordRuntime::GripperPanelManager::poll(int timeoutMs, ButtonSnapshot *snapshot)
{
    ButtonSnapshot current;
    bool received = false;
    for (size_t index = 0; index < drivers_.size(); ++index)
    {
        auto &driver = drivers_[index];
        maybeReconnectDriver(index);
        recordConnectionEventIfChanged(index, driver != nullptr && driver->isConnected());
        if (!driver->isConnected())
        {
            continue;
        }
        const uint64_t nowMs = RecordRuntime::currentSteadyMs();
        if (index < delayedStateRequestDueMs_.size() &&
            delayedStateRequestDueMs_[index] != 0 &&
            nowMs >= delayedStateRequestDueMs_[index])
        {
            driver->requestState();
            delayedStateRequestDueMs_[index] = 0;
        }
        if (index == inputDriverIndex_)
        {
            // Only the designated input side is allowed to consume the poll budget.
            received = driver->pollOnce(timeoutMs) || received;
        }
        if (index != inputDriverIndex_)
        {
            continue;
        }

        const auto state = driver->getSnapshot();
        if (state.keyPressed.size() > kBtnUpKeyIndex)
        {
            current.upPressed = state.keyPressed[kBtnUpKeyIndex];
        }
        if (state.keyPressed.size() > kBtnDownKeyIndex)
        {
            current.downPressed = state.keyPressed[kBtnDownKeyIndex];
        }
    }

    if (snapshot != nullptr)
    {
        *snapshot = current;
    }
    return received;
}

bool RecordRuntime::GripperPanelManager::getButtonsForPortToken(const std::string &token,
                                                                ButtonSnapshot *snapshot) const
{
    if (snapshot != nullptr)
    {
        *snapshot = {};
    }
    if (token.empty())
    {
        return false;
    }

    for (const auto &driver : drivers_)
    {
        if (driver == nullptr || !driver->isConnected())
        {
            continue;
        }
        if (driver->getPort().find(token) == std::string::npos)
        {
            continue;
        }

        const auto state = driver->getSnapshot();
        if (snapshot != nullptr)
        {
            if (state.keyPressed.size() > kBtnUpKeyIndex)
            {
                snapshot->upPressed = state.keyPressed[kBtnUpKeyIndex];
            }
            if (state.keyPressed.size() > kBtnDownKeyIndex)
            {
                snapshot->downPressed = state.keyPressed[kBtnDownKeyIndex];
            }
        }
        return true;
    }
    return false;
}

bool RecordRuntime::GripperPanelManager::requestStateForSide(const std::string &side)
{
    const int index = findDriverIndexForSide(side);
    if (index < 0)
    {
        return false;
    }

    auto &driver = drivers_[static_cast<size_t>(index)];
    return driver != nullptr && driver->isConnected() && driver->requestState();
}

bool RecordRuntime::GripperPanelManager::readRuntimeIdentityForSide(
    const std::string &side,
    std::string *serialNumber,
    bool *hasSerialNumber,
    std::string *errorMessage)
{
    if (serialNumber != nullptr)
    {
        serialNumber->clear();
    }
    if (hasSerialNumber != nullptr)
    {
        *hasSerialNumber = false;
    }
    const int index = findDriverIndexForSide(side);
    if (index < 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "no HMI driver for side=" + side;
        }
        return false;
    }

    auto &driver = drivers_[static_cast<size_t>(index)];
    if (driver == nullptr || !driver->isConnected())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "HMI driver disconnected for side=" + side;
        }
        return false;
    }

    const bool serialOk = driver->readSerialNumber(serialNumber);
    if (serialOk)
    {
        if (hasSerialNumber != nullptr)
        {
            *hasSerialNumber = true;
        }
    }
    else
    {
        if (hasSerialNumber != nullptr)
        {
            *hasSerialNumber = false;
        }
        if (serialNumber != nullptr)
        {
            serialNumber->clear();
        }
    }

    if (!serialOk)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = driver->getLastCommandError();
        }
        return false;
    }

    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

bool RecordRuntime::GripperPanelManager::readCalibrationForSide(
    const std::string &side, gripper_hmi::GripperCalibrationPayload *payload)
{
    const int index = findDriverIndexForSide(side);
    if (payload == nullptr || index < 0 || drivers_[index] == nullptr ||
        !drivers_[index]->isConnected()) return false;
    return drivers_[index]->readCalibrationPayload(payload);
}

bool RecordRuntime::GripperPanelManager::readFirmwareVersionForSide(
    const std::string &side, std::string *version)
{
    if (version == nullptr)
    {
        return false;
    }
    version->clear();
    const int index = findDriverIndexForSide(side);
    if (index < 0 || drivers_[index] == nullptr || !drivers_[index]->isConnected())
    {
        return false;
    }
    return drivers_[index]->readFirmwareVersion(version);
}

std::vector<RecordRuntime::GripperPanelManager::ConnectionEvent> RecordRuntime::GripperPanelManager::consumeConnectionEvents()
{
    std::vector<ConnectionEvent> events = pendingConnectionEvents_;
    pendingConnectionEvents_.clear();
    return events;
}

bool RecordRuntime::GripperPanelManager::isSideReadyForRefresh(const std::string &side, uint64_t activeTimeoutMs) const
{
    const int index = findDriverIndexForSide(side);
    if (index < 0)
    {
        return false;
    }

    const auto &driver = drivers_[static_cast<size_t>(index)];
    if (driver == nullptr || !driver->isConnected())
    {
        return false;
    }

    const auto snapshot = driver->getSnapshot(activeTimeoutMs);
    return snapshot.active;
}

bool RecordRuntime::GripperPanelManager::setBeepEnabledForSide(const std::string &side, bool enabled)
{
    const GripperBeepState nextState = enabled
                                           ? GripperBeepState{
                                                 GripperHmiDriver::kDefaultBeepDuty,
                                                 GripperHmiDriver::kDefaultBeepFrequency,
                                             }
                                           : GripperBeepState{0, 0};
    return setBeepStateForSide(side, nextState);
}

bool RecordRuntime::GripperPanelManager::setBeepStateForSide(
    const std::string &side,
    const GripperBeepState &state)
{
    bool wroteAny = false;
    for (size_t index = 0; index < drivers_.size(); ++index)
    {
        auto &driver = drivers_[index];
        if (index < currentDriverBeepStates_.size() &&
            sideForPort(driver != nullptr ? driver->getPort() : std::string()) == side)
        {
            currentDriverBeepStates_[index] = state;
        }
        if (driver == nullptr || !driver->isConnected())
        {
            if (state.duty == 0 && driver != nullptr &&
                sideForPort(driver->getPort()) == side)
            {
                driver->silenceBeep();
            }
            continue;
        }
        if (sideForPort(driver->getPort()) != side)
        {
            continue;
        }
        wroteAny = driver->setBeepState(state) || wroteAny;
    }
    return wroteAny;
}

bool RecordRuntime::GripperPanelManager::silenceBeepForSide(const std::string &side)
{
    bool found = false;
    bool allSilenced = true;
    for (size_t index = 0; index < drivers_.size(); ++index)
    {
        auto &driver = drivers_[index];
        if (driver == nullptr || sideForPort(driver->getPort()) != side)
        {
            continue;
        }
        found = true;
        if (index < currentDriverBeepStates_.size())
        {
            currentDriverBeepStates_[index] = GripperBeepState{0, 0};
        }
        allSilenced = confirmBeepSilenced(index, side) && allSilenced;
    }
    return found && allSilenced;
}

bool RecordRuntime::GripperPanelManager::confirmBeepSilenced(size_t index,
                                                              const std::string &side)
{
    if (index >= drivers_.size() || drivers_[index] == nullptr)
    {
        return false;
    }

    auto &driver = drivers_[index];
    if (!driver->isConnected())
    {
        driver->silenceBeep();
        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "cannot confirm hmi beep silence while disconnected"
            << " side=" << side
            << " port=" << driver->getPort()
            << std::endl).str());
        return false;
    }

    GripperHmiSnapshot lastSnapshot = driver->getSnapshot();
    for (int attempt = 1; attempt <= kBeepSilenceRetryLimit; ++attempt)
    {
        const uint64_t previousStateCount = lastSnapshot.beepStateCount;
        const bool commandOk = driver->silenceBeep();
        const bool requestOk = driver->requestState();
        bool receivedNewState = false;

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kBeepSilenceConfirmMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            driver->pollOnce(10);
            lastSnapshot = driver->getSnapshot();
            if (lastSnapshot.beepStateCount > previousStateCount)
            {
                receivedNewState = true;
                if (lastSnapshot.beepState.duty == 0)
                {
                    return true;
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        DM_LOG_WARN("{}", (::DA::utils::LogString()
            << "failed to confirm hmi beep silence"
            << " side=" << side
            << " port=" << driver->getPort()
            << " attempt=" << attempt
            << " command_ok=" << boolText(commandOk)
            << " request_ok=" << boolText(requestOk)
            << " new_state=" << boolText(receivedNewState)
            << " beep_duty=" << lastSnapshot.beepState.duty
            << " beep_frequency=" << lastSnapshot.beepState.frequency
            << std::endl).str());
    }
    return false;
}

void RecordRuntime::GripperPanelManager::silenceBeep()
{
    for (size_t index = 0; index < drivers_.size(); ++index)
    {
        if (index < currentDriverBeepStates_.size())
        {
            currentDriverBeepStates_[index] = GripperBeepState{0, 0};
        }
        if (drivers_[index] != nullptr)
        {
            confirmBeepSilenced(index, sideForPort(drivers_[index]->getPort()));
        }
    }
}

void RecordRuntime::GripperPanelManager::setLedColor(uint8_t red, uint8_t green, uint8_t blue)
{
    currentLedColor_ = GripperLedColor{red, green, blue};
    hasDirectLedColor_ = true;
    hasLedEffect_ = false;
    perSideLedStates_.clear();
    for (auto &driver : drivers_)
    {
        if (driver != nullptr)
        {
            driver->setLedColor(red, green, blue);
        }
    }
}

void RecordRuntime::GripperPanelManager::setLedEffect(const GripperLedEffect &effect)
{
    currentLedEffect_ = effect;
    hasLedEffect_ = true;
    hasDirectLedColor_ = false;
    perSideLedStates_.clear();
    for (auto &driver : drivers_)
    {
        if (driver != nullptr)
        {
            driver->setLedEffect(effect);
        }
    }
}

void RecordRuntime::GripperPanelManager::setLedColorForSide(const std::string &side, uint8_t red, uint8_t green, uint8_t blue)
{
    const int index = findDriverIndexForSide(side);
    if (index < 0)
    {
        return;
    }
    if (perSideLedStates_.size() < drivers_.size())
    {
        perSideLedStates_.resize(drivers_.size());
    }
    auto &sideLed = perSideLedStates_[static_cast<size_t>(index)];
    sideLed.color = GripperLedColor{red, green, blue};
    sideLed.hasColor = true;
    sideLed.hasEffect = false;
    if (drivers_[static_cast<size_t>(index)] != nullptr)
    {
        drivers_[static_cast<size_t>(index)]->setLedColor(red, green, blue);
    }
}

void RecordRuntime::GripperPanelManager::setLedEffectForSide(const std::string &side, const GripperLedEffect &effect)
{
    const int index = findDriverIndexForSide(side);
    if (index < 0)
    {
        return;
    }
    if (perSideLedStates_.size() < drivers_.size())
    {
        perSideLedStates_.resize(drivers_.size());
    }
    auto &sideLed = perSideLedStates_[static_cast<size_t>(index)];
    sideLed.effect = effect;
    sideLed.hasEffect = true;
    sideLed.hasColor = false;
    if (drivers_[static_cast<size_t>(index)] != nullptr)
    {
        drivers_[static_cast<size_t>(index)]->setLedEffect(effect);
    }
}

void RecordRuntime::GripperPanelManager::turnOff()
{
    setLedColor(0, 0, 0);
}

RecordRuntime::HmiLedController::HmiLedController(GripperPanelManager *panelManager)
    : panelManager_(panelManager)
{
}

RecordRuntime::HmiLedController::~HmiLedController()
{
    stop();
}

void RecordRuntime::HmiLedController::start()
{
    if (panelManager_ == nullptr)
    {
        return;
    }
}

void RecordRuntime::HmiLedController::stop()
{
    if (panelManager_ != nullptr)
    {
        panelManager_->turnOff();
    }
}

void RecordRuntime::HmiLedController::setState(LedState state, double progress)
{
    if (panelManager_ == nullptr)
    {
        return;
    }

    if (state == LedState::Ready && (!hasState_ || currentState_ != LedState::Ready))
    {
        readyBreathPhaseOffsetMs_ =
            GripperLedEffectRenderer::readyBreathEntryPhaseOffsetMs(RecordRuntime::currentSteadyMs());
    }
    currentState_ = state;
    hasState_ = true;

    GripperLedEffect effect = makeLedEffect(state, progress);
    if (state == LedState::Ready)
    {
        effect.phaseOffsetMs = readyBreathPhaseOffsetMs_;
    }
    panelManager_->setLedEffect(effect);
}

}  // namespace DA::Ugripper
