#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ugripper::runtime {

class EpisodeManager
{
public:
    struct GripperRuntimeState
    {
        std::string side;
        bool connected = false;
        bool hasSerialNumber = false;
        std::string serialNumber;
        std::string lastError;
    };

    struct MainCameraRuntimeState
    {
        std::string cameraName;
        std::string devicePath;
        bool enabled = true;
        bool present = false;
        std::string serialNumber;
        bool calibrationPayloadCached = false;
        std::array<uint8_t, 1024> calibrationPayload{};
        std::string lastError;
    };

    struct TactileCameraRuntimeState
    {
        std::string cameraName;
        std::string side;
        std::string devicePath;
        bool present = false;
        std::string serialNumber;
        std::string lastError;
    };

    struct TactileValidationFinding
    {
        std::string cameraName;
        std::string serialNumber;
        std::string side;
        std::string sensorSlot;
        bool damaged = false;
        bool warningActive = false;
        bool warningTriggered = false;
        std::string detail;
        std::string audioCommand;
    };

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
                   std::string updaterVersion);

    bool initialize();
    std::string createNextEpisodeDir();
    void setGripperRuntimeStates(const std::array<GripperRuntimeState, 2> &states);
    void setMainCameraRuntimeStates(const std::array<MainCameraRuntimeState, 3> &states);
    void setTactileCameraRuntimeStates(const std::array<TactileCameraRuntimeState, 4> &states);
    void setTaskMarkers(std::optional<double> taskStartS,
                        std::optional<double> taskStopS);
    void markTactileReferencePendingForSide(const std::string &side,
                                            const std::string &reason);
    bool prepareEpisode(const std::string &episodeDir,
                        bool resetRecording,
                        const std::string &resetSourceDir,
                        std::string *errorMessage) const;
    bool validateEpisode(const std::string &episodeDir,
                         std::string *errorMessage,
                         std::vector<std::string> *errorTypes = nullptr,
                         std::vector<TactileValidationFinding> *tactileFindings = nullptr) const;
    void validateTactileEpisode(const std::string &episodeDir,
                                std::vector<TactileValidationFinding> *tactileFindings) const;
    bool writeFinalMetadata(const std::string &episodeDir,
                            bool qualityOk,
                            const std::string &qualityErrorMessage,
                            const std::string &qualityErrorType,
                            std::string *errorMessage) const;
    std::string finalizeEpisodeDir(const std::string &episodeDir, std::string *errorMessage) const;
    const std::string &dataRoot() const;

private:
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
    std::string dataRoot_;
    std::string episodeRoot_;
    std::array<GripperRuntimeState, 2> gripperRuntimeStates_{};
    std::array<MainCameraRuntimeState, 3> mainCameraRuntimeStates_{};
    std::array<TactileCameraRuntimeState, 4> tactileCameraRuntimeStates_{};
    std::optional<double> taskStartS_;
    std::optional<double> taskStopS_;
};

}  // namespace ugripper::runtime
