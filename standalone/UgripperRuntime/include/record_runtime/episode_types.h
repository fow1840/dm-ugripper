#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace DA::Ugripper {

struct GripperRuntimeState
{
    std::string side;
    bool connected = false;
    bool hasSerialNumber = false;
    std::string serialNumber;
    std::string lastError;
    bool calibrationPayloadCached = false;
    std::array<uint8_t, 1024> calibrationPayload{};
    unsigned calibrationReadAttempts = 0;
    uint64_t nextCalibrationReadMs = 0;
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

struct EpisodeManagerDependencies
{
    uint64_t (*currentEpochMs)();
    bool (*fileExistsAndNotEmpty)(const std::string &path);
};

}  // namespace DA::Ugripper
