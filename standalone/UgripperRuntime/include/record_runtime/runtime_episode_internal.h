#pragma once

#include "record_runtime/tactile_residual.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace DA::Ugripper::detail {

struct EpisodeVideoArtifact
{
    const char *cameraName;
    const char *fileName;
};

inline constexpr std::array<EpisodeVideoArtifact, 9> kAllEpisodeVideoArtifacts = {{
    {"left_cam_main", "cam_left.mkv"},
    {"right_cam_main", "cam_right.mkv"},
    {"chest_cam_main", "cam_chest.mkv"},
    {"left_stereo", "stereo_left.mkv"},
    {"right_stereo", "stereo_right.mkv"},
    {"left_tcam_l", "tcam_left_l.mkv"},
    {"left_tcam_r", "tcam_left_r.mkv"},
    {"right_tcam_l", "tcam_right_l.mkv"},
    {"right_tcam_r", "tcam_right_r.mkv"},
}};

struct VideoProbeResult
{
    std::string cameraName;
    std::string fileName;
    double startTimeSec = 0.0;
    double durationSec = 0.0;
    double spanSec = 0.0;
};

struct VideoProbeTaskResult
{
    bool ok = false;
    VideoProbeResult probe;
    std::string errorMessage;
};

struct EncoderTailCheckTarget
{
    const char *side;
    const char *mcapFileName;
    const char *encoderTopic;
    std::array<const char *, 4> referenceCameraNames;
};

inline constexpr std::array<EncoderTailCheckTarget, 2> kEncoderTailCheckTargets = {{
    {"left",
     "sensor_left.mcap",
     "encoder_left",
     {"left_cam_main", "left_stereo", "left_tcam_l", "left_tcam_r"}},
    {"right",
     "sensor_right.mcap",
     "encoder_right",
     {"right_cam_main", "right_stereo", "right_tcam_l", "right_tcam_r"}},
}};

struct FaysTailCheckTarget
{
    const char *side;
    const char *mcapFileName;
    const char *imuTopic;
    const char *cameraTopic;
};

inline constexpr std::array<FaysTailCheckTarget, 2> kFaysTailCheckTargets = {{
    {"left", "fays_data_left.mcap", "i", "c"},
    {"right", "fays_data_right.mcap", "i", "c"},
}};

struct FaysMcapSummary
{
    uint64_t lastImuLogTimeNs = 0;
    uint64_t firstCameraLogTimeNs = 0;
    uint64_t lastCameraLogTimeNs = 0;
    uint64_t firstCameraPublishTimeNs = 0;
    uint64_t lastCameraPublishTimeNs = 0;
    uint64_t imuMessageCount = 0;
    uint64_t cameraMessageCount = 0;
    uint64_t spanNs = 0;
    uint64_t cameraSpanNs = 0;
    uint64_t cameraPublishSpanNs = 0;
};

struct TailCheckTaskResult
{
    bool ok = false;
    std::string detail;
    std::string errorMessage;
    std::string errorType = "frame_loss";
};

struct CommandCaptureResult
{
    bool success = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;
    std::string errorOutput;
    bool outputTruncated = false;
};

struct TactileCalibrationTarget
{
    const char *cameraName;
    const char *side;
    const char *sensorSlot;
    const char *devicePath;
    const char *jsonPath;
    const char *serialPlaceholder;
};

inline constexpr std::array<TactileCalibrationTarget, 4> kTactileCalibrationTargets = {{
    {"left_tcam_l", "left", "l", "/dev/tcam_left_l", "observation.images.tcam_left_l", "{{LEFT_TCAM_L_SERIAL}}"},
    {"left_tcam_r", "left", "r", "/dev/tcam_left_r", "observation.images.tcam_left_r", "{{LEFT_TCAM_R_SERIAL}}"},
    {"right_tcam_l", "right", "l", "/dev/tcam_right_l", "observation.images.tcam_right_l", "{{RIGHT_TCAM_L_SERIAL}}"},
    {"right_tcam_r", "right", "r", "/dev/tcam_right_r", "observation.images.tcam_right_r", "{{RIGHT_TCAM_R_SERIAL}}"},
}};

inline constexpr int64_t kEncoderTailMaxLagNs = 1000LL * 1000LL * 1000LL;
inline constexpr int64_t kFaysImuTailMaxLagNs = 1000LL * 1000LL * 1000LL;
inline constexpr int64_t kEncoderTailWindowNs = 1000LL * 1000LL * 1000LL;
inline constexpr size_t kEncoderTailChunkScanLimit = 4;
inline constexpr int64_t kFaysMcapMinSpanNs = 500LL * 1000LL * 1000LL;
inline constexpr size_t kFaysTailChunkScanLimit = 4;
inline constexpr uint16_t kFaysImuChannelId = 1;
inline constexpr uint16_t kFaysCameraChannelId = 2;
inline constexpr uint64_t kFaysImuPayloadBytes = sizeof(double) * 6;
inline constexpr uint64_t kFaysCameraPayloadBytes = sizeof(uint32_t);
inline constexpr double kMinReasonableVideoSpanSec = 0.2;
inline constexpr double kMaxVideoSpanGapSec = 5.0;
inline constexpr size_t kTactileHistoryWindow = 3;
inline constexpr double kTactileEpisodeProbeSec = 0.12;
inline constexpr int kTactileSnapshotTimeoutMs = 2500;
inline constexpr int kVideoProbeTimeoutMs = 3000;

std::filesystem::path episodeTimingPath(const std::filesystem::path &episodeDir);
int ChildFileDescriptorLimit();
void CloseChildFileDescriptors(int fdLimit);
std::string sanitizeFileComponent(const std::string &text);
std::filesystem::path tactileReferenceDir(const std::filesystem::path &root);
std::filesystem::path tactilePersistentDir(const std::filesystem::path &root);
std::filesystem::path tactileHistoryPath(const std::filesystem::path &root);
std::filesystem::path tactileReferenceRawPath(const std::filesystem::path &root,
                                              const std::string &serial);
std::filesystem::path tactilePersistentRawPath(const std::filesystem::path &root,
                                               const std::string &serial);
std::string currentBootId();
std::string stripEpisodeTempSuffix(const std::string &name);
void flushEpisodeDirectoriesToDisk(const std::filesystem::path &episodeDir, const char *phaseLabel);
bool writeTextFileAtomically(const std::filesystem::path &path,
                             const std::string &content,
                             std::string *errorMessage);
bool loadJsonFile(const std::string &path, nlohmann::json *output, std::string *errorMessage);
std::vector<EpisodeVideoArtifact> activeEpisodeVideoArtifacts(bool chestCameraEnabled,
                                                               bool stereoEnabled);
const char *episodeVideoFileNameForCamera(const char *cameraName);
bool isStereoCameraName(const char *cameraName);
bool commandExists(const std::string &command);
int64_t steadyNowMs();
void logPerf(const std::string &message);
std::string formatSeconds(double value);
bool validateMetadataVideoDetails(const std::filesystem::path &metadataPath,
                                  const std::vector<EpisodeVideoArtifact> &artifacts,
                                  std::string *errorMessage);
std::vector<VideoProbeTaskResult> probeVideoFilesParallel(
    const std::string &episodeDir,
    const std::vector<EpisodeVideoArtifact> &artifacts);
bool loadEffectiveVideoDurationSec(const std::string &episodeDir,
                                   const EpisodeVideoArtifact &artifact,
                                   const VideoProbeResult &probe,
                                   const nlohmann::json *timingRoot,
                                   double fallbackDurationSec,
                                   double *durationSec,
                                   std::string *source,
                                   std::string *errorMessage);
bool writeVideoProbeCacheToTimingFile(const std::string &episodeDir,
                                      const std::vector<VideoProbeResult> &probes,
                                      std::string *errorMessage);
bool loadCachedVideoProbe(const nlohmann::json &timing,
                          const EpisodeVideoArtifact &artifact,
                          VideoProbeResult *probe);
bool probeVideoFile(const std::string &filePath,
                    VideoProbeResult *result,
                    std::string *errorMessage);
bool loadLastTopicLogTimeFromTailChunks(const std::string &mcapPath,
                                        const std::string &topic,
                                        uint64_t *lastLogTimeNs,
                                        uint64_t *messageCount,
                                        std::string *errorMessage);
bool loadFaysMcapSummary(const std::string &mcapPath,
                         const std::string &imuTopic,
                         const std::string &cameraTopic,
                         FaysMcapSummary *summary,
                         std::string *errorMessage);
std::optional<std::vector<uint8_t>> captureTactileGrayFrame(
    const std::vector<std::string> &inputArguments,
    double seekSeconds,
    std::string *detail);
bool readBinaryFileExact(const std::filesystem::path &path,
                         size_t expectedSize,
                         std::vector<uint8_t> *bytes,
                         std::string *errorMessage);
bool writeBinaryFile(const std::filesystem::path &path,
                     const std::vector<uint8_t> &bytes,
                     std::string *errorMessage);
std::string currentDateString();
bool writeTactileReferenceMeta(const std::filesystem::path &root,
                               const std::string &serial,
                               const TactileCalibrationTarget &target,
                               uint64_t updatedAtMs,
                               std::string *errorMessage);
bool isYuzhouMainCameraSn(const std::string &sn);
bool validateYuzhouMainCameraCalibrationPayload(const std::array<uint8_t, 1024> &payload,
                                                std::string *detail);
std::string joinStrings(const std::vector<std::string> &items, const char *separator);
std::string trim(std::string text);
const nlohmann::json *findJsonPathConst(const nlohmann::json *root,
                                       const std::string &path);
size_t gripperStateIndexForSide(const std::string &side);
nlohmann::json makeRuntimeTactileCalibrationEntry(const std::string &serial);
nlohmann::json makeMainCameraCalibrationEntryFromPayload(
    const std::array<uint8_t, 1024> &payload);
nlohmann::json makeDefaultMainCameraCalibrationEntry();
bool loadFaysCalibrationFromStatus(const std::string &stereoStatusFile,
                                   const std::string &cameraName,
                                   nlohmann::json *calibration,
                                   std::string *statusMessage);
nlohmann::json makeFaysStereoCalibrationEntry(const nlohmann::json &faysCalibration);
nlohmann::json makeDefaultStereoCalibrationEntry();
nlohmann::json makeFaysImuCalibrationEntry(const nlohmann::json &faysCalibration);
nlohmann::json makeDefaultFaysImuCalibrationEntry();
nlohmann::json makeLockedCalibrationInfo(const std::string &calibrationStatus);

}  // namespace DA::Ugripper::detail
