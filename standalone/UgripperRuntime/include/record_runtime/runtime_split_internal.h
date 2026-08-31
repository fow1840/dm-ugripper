#pragma once

#include "record_runtime.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ugripper::runtime::record_runtime_detail {

struct ProcTaskDState
{
    std::string taskId;
    std::string comm;
    std::string wchan;
};

struct YuzhouMainCameraIdentity
{
    std::string cameraName;
    std::string devicePath;
    std::string resolvedTarget;
    std::string serialNumber;
    bool calibrationPayloadCached = false;
    std::array<uint8_t, 1024> calibrationPayload{};
};

extern const uint64_t kSystemActionResultWaitMs;
extern const uint64_t kRestoreUsbSymlinkPollIntervalMs;
extern const uint64_t kRestoreUsbSymlinkMaxWaitMs;
extern const uint64_t kRestoreUsbReadyCheckDelayMs;
extern const uint64_t kRestoreUsbFailureAlarmDurationMs;
extern const uint64_t kRestoreUsbManualInsertGraceMs;
extern const uint64_t kRestoreUsbTriggerStableMs;
extern const uint64_t kRestoreUsbPreflightFailureCooldownMs;
extern const int kRestoreUsbMaxAttemptsPerError;
extern const uint64_t kCameraRecorderDStateFaultMs;
extern const uint64_t kCameraRecorderDStateLogIntervalMs;
extern const uint64_t kGripperRefreshActiveTimeoutMs;
extern const uint64_t kMainCameraRefreshDelayMs;
extern const uint64_t kMainCameraRefreshRetryMs;
extern const uint64_t kTactileSerialRefreshRetryMs;
extern const int kBeepSilenceRetryLimit;
extern const int kBeepSilenceConfirmMs;
extern const char *const kMainCameraV4l2KernelHangKey;
extern const char *const kMainCameraV4l2StartupFailureKey;
extern const char *const kMainCameraShortStreamKey;
extern const std::array<const char *, 6> kLeftCriticalDevicePaths;
extern const std::array<const char *, 6> kRightCriticalDevicePaths;

std::optional<ProcTaskDState> FindFirstDStateTask(int pid);
bool IsCameraRecorderKernelHangReason(const std::string &reason);
bool IsCameraRecorderMainCameraFault(const ugripper::runtime::HealthFault &fault);
bool IsImmediateRestoreUsbTriggerCause(const std::string &cause);
ugripper::runtime::HardwareFaultSide restoreUsbSideForText(const std::string &text);
const char *restoreUsbSideName(ugripper::runtime::HardwareFaultSide side);
std::string compactRestoreUsbEvidence(std::string detail);
bool isCriticalDeviceMissingEvidence(const std::string &detail);
const char *boolText(bool value);
std::filesystem::path cameraRecorderFaultPath(const std::filesystem::path &episodeDir);
bool isStereoDevicePath(const char *path);
std::vector<const char *> activeCriticalDevicePaths(bool chestCameraEnabled, bool stereoEnabled);
bool flushDirectoryToDisk(const std::filesystem::path &path, std::error_code *error);
std::string shellQuote(const std::string &value);
std::string resolveDeviceNodeTarget(const std::string &devicePath);
std::optional<std::string> readTactileUsbSerialFromSysfs(const std::string &devicePath,
                                                        const std::string &resolvedTarget,
                                                        std::string *detail);
std::optional<YuzhouMainCameraIdentity> readYuzhouMainCameraIdentity(
    const std::string &cameraName,
    const std::string &devicePath,
    const std::string &resolvedTarget,
    std::string *detail);

}  // namespace ugripper::runtime::record_runtime_detail
