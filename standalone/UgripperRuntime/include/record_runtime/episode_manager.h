#pragma once

#include "record_runtime/episode_types.h"
#include "record_runtime/runtime_episode_internal.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace DA::Ugripper {

class EpisodeManager
{
public:
    // Compatibility aliases keep call sites stable while the DTOs remain
    // independent from EpisodeManager's lifecycle and ownership.
    using GripperRuntimeState = DA::Ugripper::GripperRuntimeState;
    using MainCameraRuntimeState = DA::Ugripper::MainCameraRuntimeState;
    using TactileCameraRuntimeState = DA::Ugripper::TactileCameraRuntimeState;
    using TactileValidationFinding = DA::Ugripper::TactileValidationFinding;

    EpisodeManager(std::string diskRoot,
                   std::string deviceSn,
                   std::string language,
                   std::string cameraCodec,
                   bool chestCameraEnabled,
                   bool stereoEnabled,
                   std::string tactileStateDir,
                   std::string persistCalibrationFile,
                   std::string exampleCalibrationFile,
                   std::string fallbackCalibrationFile,
                   std::string stereoStatusFile,
                   std::string hardwareVersion,
                   std::string packageVersion,
                   std::string updaterVersion,
                   EpisodeManagerDependencies dependencies);

    bool Initialize();
    std::string CreateNextEpisodeDir();
    void SetGripperRuntimeStates(const std::array<GripperRuntimeState, 2> &states);
    void SetMainCameraRuntimeStates(const std::array<MainCameraRuntimeState, 3> &states);
    void SetTactileCameraRuntimeStates(const std::array<TactileCameraRuntimeState, 4> &states);
    void SetDeviceVersions(const DeviceVersions &versions, const std::string &egoSerial);
    void SetTaskMarkers(std::optional<double> taskStartS,
                        std::optional<double> taskStopS);
    void MarkTactileReferencePendingForSide(const std::string &side,
                                            const std::string &reason);
    bool PrepareEpisode(const std::string &episodeDir,
                        bool resetRecording,
                        const std::string &resetSourceDir,
                        std::string *errorMessage) const;
    bool ValidateEpisode(const std::string &episodeDir,
                         std::string *errorMessage,
                         std::vector<std::string> *errorTypes = nullptr,
                         std::vector<TactileValidationFinding> *tactileFindings = nullptr) const;
    void ValidateTactileEpisode(const std::string &episodeDir,
                                std::vector<TactileValidationFinding> *tactileFindings) const;
    bool WriteFinalMetadata(const std::string &episodeDir,
                            bool qualityOk,
                            const std::string &qualityErrorMessage,
                            const std::string &qualityErrorType,
                            std::string *errorMessage) const;
    std::string FinalizeEpisodeDir(const std::string &episodeDir, std::string *errorMessage) const;
    const std::string &GetDataRoot() const;

private:
    const std::vector<detail::VideoProbeTaskResult> &finalizationVideoProbes(const std::string &episodeDir) const;
    mutable std::string probeEpisodeKey_;
    mutable std::vector<detail::VideoProbeTaskResult> finalizationProbes_;
    bool writeFilteredCalibration(const std::string &episodeDir, std::string *errorMessage) const;
    bool prepareEpisodeOutputs(const std::string &episodeDir, std::string *errorMessage) const;
    bool updateTactileDamageMetadata(
        const std::string &episodeDir,
        const std::vector<TactileValidationFinding> &tactileFindings,
        std::string *errorMessage) const;
    const TactileCameraRuntimeState *tactileCameraStateForName(const std::string &cameraName) const;
    std::string cachedTactileSerialForName(const std::string &cameraName) const;

    std::string diskRoot_;
    std::string deviceSn_;
    std::string deviceSnLower_;
    std::string language_;
    std::string cameraCodec_;
    bool chestCameraEnabled_ = true;
    bool stereoEnabled_ = true;
    std::string tactileStateDir_;
    std::string persistCalibrationFile_;
    std::string exampleCalibrationFile_;
    std::string fallbackCalibrationFile_;
    std::string stereoStatusFile_;
    std::string hardwareVersion_;
    std::string packageVersion_;
    std::string updaterVersion_;
    DeviceVersions deviceVersions_;
    std::string egoSerial_;
    EpisodeManagerDependencies dependencies_;
    std::string dataRoot_;
    std::string episodeRoot_;
    std::array<GripperRuntimeState, 2> gripperRuntimeStates_{};
    std::array<MainCameraRuntimeState, 3> mainCameraRuntimeStates_{};
    std::array<TactileCameraRuntimeState, 4> tactileCameraRuntimeStates_{};
    std::optional<double> taskStartS_;
    std::optional<double> taskStopS_;
};

}  // namespace DA::Ugripper
