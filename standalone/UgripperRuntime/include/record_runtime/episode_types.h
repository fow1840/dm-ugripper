#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <map>

namespace DA::Ugripper {

// Metadata 3.3: preserve this order after data_version. All values are strings.
inline constexpr std::array<const char *, 12> kDeviceVersionFields = {{
    "bluetooth_gatt_server_version",
    "databot_device_joint_version",
    "device_ota_mender_version",
    "gripper_left_firmware_version",
    "gripper_right_firmware_version",
    "cam_left_firmware_version",
    "cam_right_firmware_version",
    "cam_chest_firmware_version",
    "stereo_left_firmware_version",
    "stereo_right_firmware_version",
    "ego_software_version",
    "ego_firmware_version",
}};
using DeviceVersions = std::map<std::string, std::string>;

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
