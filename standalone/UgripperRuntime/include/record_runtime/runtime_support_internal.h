#pragma once

#include "record_runtime.h"
#include "record_runtime/runtime_persistence_support_internal.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace ugripper::runtime::record_runtime_detail {

#ifndef UGRIPPER_ENABLE_STEREO
#define UGRIPPER_ENABLE_STEREO 1
#endif
#if UGRIPPER_ENABLE_STEREO
inline constexpr bool kPackageStereoEnabled = true;
inline constexpr const char *kPackageStereoBuildMarker = "UGRIPPER_STEREO_BUILD=ON";
#else
inline constexpr bool kPackageStereoEnabled = false;
inline constexpr const char *kPackageStereoBuildMarker = "UGRIPPER_STEREO_BUILD=OFF";
#endif

#ifndef UGRIPPER_ENABLE_PERF_LOG
#define UGRIPPER_ENABLE_PERF_LOG 0
#endif

inline constexpr const char *kChestCameraEnvKey = "ENABLE_CHEST_CAM_MAIN";
inline constexpr bool kPerfLogEnabled = UGRIPPER_ENABLE_PERF_LOG != 0;
inline constexpr uint64_t kActionDebounceMs = 80;
inline constexpr uint64_t kButtonPressDebounceMs = 40;
inline constexpr uint64_t kButtonReleaseDebounceMs = 40;
inline constexpr uint64_t kLongPressThresholdMs = 1000;
inline constexpr uint64_t kRecordingTaskMarkThresholdMs = 800;
inline constexpr uint64_t kEgoBindingChordHoldMs = 2000;
inline constexpr uint64_t kEgoProbeIntervalMs = 2000;
inline constexpr int kEgoProbeFailureThreshold = 2;
inline constexpr uint64_t kDualLongPressThresholdMs = 4000;
inline constexpr uint64_t kShutdownPromptThresholdMs = 2000;
inline constexpr uint64_t kPostUmountAudioDelayMs = 2000;
inline constexpr uint64_t kAudioPlayerRestartIntervalMs = 2000;
inline constexpr uint64_t kAudioPlayerReadyGraceMs = 3000;
inline constexpr uint64_t kStereoDaemonRestartIntervalMs = 2000;
inline constexpr uint64_t kStereoStartupReadyTimeoutMs = 40000;
inline constexpr uint64_t kStereoFinalizeWaitPollMs = 100;
inline constexpr uint64_t kRecordControlDebounceMs = 300;
inline constexpr uint64_t kPhysicalRecordDoubleClickMs = 1000;
inline constexpr int kOperatorMarkBeepOnMs = 120;
inline constexpr int kOperatorMarkBeepOffMs = 120;
inline constexpr int kOperatorMarkBeepSilenceRetryAttempts = 5;
inline constexpr int kOperatorMarkBeepSilenceRetryIntervalMs = 80;
inline constexpr uint8_t kOperatorMarkLedRed = 180;
inline constexpr uint8_t kOperatorMarkLedGreen = 0;
inline constexpr uint8_t kOperatorMarkLedBlue = 255;
inline constexpr uint64_t kEgoFinalizeWaitPollMs = 100;
inline constexpr uint64_t kEgoStartAckWaitMs = 5000;
inline constexpr uint64_t kEgoStartAckPollMs = 50;
inline constexpr uint64_t kHealthCheckIntervalMs = 1000;
inline constexpr uint64_t kHmiActiveTimeoutMs = 5500;
inline constexpr uint8_t kYuzhouXuUnitId = 0x03;
inline constexpr uint8_t kYuzhouXuSelector = 0x17;
inline constexpr size_t kYuzhouXuPacketSize = 9;
inline constexpr size_t kYuzhouPayloadChunkSize = 8;
inline constexpr size_t kYuzhouPayloadSize = 1024;
inline constexpr size_t kYuzhouPayloadChunkCount = kYuzhouPayloadSize / kYuzhouPayloadChunkSize;
inline constexpr size_t kYuzhouSnOffset = 0x010;
inline constexpr size_t kYuzhouSnLength = 16;
inline constexpr const char *kEpisodeTimingFileName = ".recording_timing.json";
inline constexpr const char *kEpisodeTimingShmPrefix = "ugripper_recording_timing_";
inline constexpr const char *kCameraRecorderFaultShmPrefix = "ugripper_camera_recorder_fault_";
inline constexpr const char *kEgoSyncStatusFileName = "ego_sync.json";

extern bool g_perfLogEnabled;

bool IsStereoStartupFaultKey(const std::string &key);
bool perfLogEnabled();
bool isFalseLikeValue(const std::string &value);
bool IsDiskHealthFault(const ugripper::runtime::HealthFault &fault);
std::optional<ugripper::runtime::HealthFault> detectDiskHealthFault(const std::string &diskRootPath);
std::vector<std::string> sessionCameraStreams(bool chestCameraEnabled);
std::string joinCsv(const std::vector<std::string> &items);
std::vector<std::string> resolvePythonCommand();
bool hasAdbUsbInterface();

}  // namespace ugripper::runtime::record_runtime_detail
