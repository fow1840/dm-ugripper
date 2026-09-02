#pragma once

#include <cstdint>

namespace camera_recorder::camera_time {

// Signed timestamp offsets for CameraRecorder streams. Positive values move
// timestamps later; negative values compensate observation latency by moving
// timestamps earlier. Update these values when a new experiment is accepted,
// then rebuild.
// Main-camera absolute time now comes from the camera XU RTC plus the frame
// encoder PTS, so no host-observation latency is added after capture.
inline constexpr int64_t kMainCameraLatencyUs = 8'000;
inline constexpr int64_t kTactileCameraLatencyUs = -35'000;

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
