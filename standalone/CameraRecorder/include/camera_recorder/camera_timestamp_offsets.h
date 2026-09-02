#pragma once

#include <cstdint>

namespace camera_recorder::camera_time {

// Host-camera latency compensation is currently disabled.
// Update these values when a new experiment is accepted, then rebuild.
inline constexpr int64_t kMainCameraLatencyUs = 0;
inline constexpr int64_t kTactileCameraLatencyUs = 0;

enum class CameraTimestampSource {
    Main,
    Tactile,
};

constexpr int64_t CameraLatencyUs(CameraTimestampSource source)
{
    switch (source)
    {
    case CameraTimestampSource::Main:
        return kMainCameraLatencyUs;
    case CameraTimestampSource::Tactile:
        return kTactileCameraLatencyUs;
    }
    return 0;
}

constexpr int64_t AddCameraLatencyUs(int64_t timestampUs, CameraTimestampSource source)
{
    return timestampUs + CameraLatencyUs(source);
}

}  // namespace camera_recorder::camera_time
