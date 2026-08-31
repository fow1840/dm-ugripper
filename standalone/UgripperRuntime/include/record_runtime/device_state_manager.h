#ifndef UGRIPPER_RUNTIME_DEVICE_STATE_MANAGER_H
#define UGRIPPER_RUNTIME_DEVICE_STATE_MANAGER_H

#include "record_runtime/episode_types.h"

#include <array>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace DA::Ugripper
{

class DeviceStateManager
{
public:
    struct MainCameraRefreshResult
    {
        std::string cameraName;
        std::string devicePath;
        std::string resolvedTarget;
        std::string serialNumber;
        bool calibrationPayloadCached = false;
        std::array<uint8_t, 1024> calibrationPayload{};
        std::string errorMessage;
    };

    struct MainCameraRuntimeCache
    {
        std::string cameraName;
        std::string devicePath;
        bool enabled = true;
        bool present = false;
        std::string resolvedTarget;
        std::string serialNumber;
        bool calibrationPayloadCached = false;
        std::array<uint8_t, 1024> calibrationPayload{};
        std::string lastError;
        uint64_t nextRefreshAllowedMs = 0;
        std::future<MainCameraRefreshResult> refreshFuture;
    };

    struct TactileCameraRuntimeCache
    {
        std::string cameraName;
        std::string side;
        std::string devicePath;
        bool present = false;
        std::string resolvedTarget;
        std::string serialNumber;
        std::string lastError;
        uint64_t nextRefreshAllowedMs = 0;
    };

    struct State
    {
        std::array<GripperRuntimeState, 2> gripperRuntimeStates{};
        std::array<bool, 2> pendingGripperRefresh{};
        std::vector<MainCameraRuntimeCache> mainCameraRuntimeStates;
        std::vector<TactileCameraRuntimeCache> tactileCameraRuntimeStates;
    };

    State &GetState() noexcept
    {
        return state_;
    }

    const State &GetState() const noexcept
    {
        return state_;
    }

private:
    State state_;
};

} // namespace DA::Ugripper

#endif
