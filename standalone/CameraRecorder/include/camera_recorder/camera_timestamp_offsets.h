#pragma once

#include <cstdint>
#include <string_view>

namespace camera_recorder::camera_time {

// Signed timestamp offsets for CameraRecorder streams. Positive values move
// timestamps later; negative values compensate observation latency by moving
// timestamps earlier. Update these values when a new experiment is accepted,
// then rebuild.
// Main-camera absolute time now comes from the camera XU RTC plus the frame
// encoder-completion PTS. Move its absolute anchor 28 ms earlier to estimate
// shutter time: measured +36 ms included the previous +8 ms offset.
// Cadence correction changes only the relative frame timing.
inline constexpr int64_t kMainCameraLatencyUs = -28'000;
// Tactile now uses the V4L2 capture clock rather than first encoded-output
// observation. The old -35ms included encoding latency and must not carry over.
// Default capture-clock correction for streams without a per-view calibration.
// Clock-rate drift against the external stopwatch is handled offline only.
inline constexpr int64_t kTactileCameraLatencyUs = -2'700;

enum class CameraTimestampSource {
    Main,
    Tactile,
};

// First ten valid optical samples, episode_20260917_0002. These fixed
// per-view corrections do not change cadence or compensate clock-rate drift.
constexpr int64_t CameraLatencyUs(CameraTimestampSource source, std::string_view name = {})
{
    switch (source)
    {
    case CameraTimestampSource::Main:
        if (name == "left_cam_main") return -27'200;
        if (name == "right_cam_main") return -29'200;
        return kMainCameraLatencyUs;
    case CameraTimestampSource::Tactile:
        if (name == "left_tcam_l") return -1'500;
        if (name == "left_tcam_r" || name == "right_tcam_l") return -2'100;
        if (name == "right_tcam_r") return -1'800;
        return kTactileCameraLatencyUs;
    }
    return 0;
}

constexpr int64_t AddCameraLatencyUs(int64_t timestampUs, CameraTimestampSource source,
                                   std::string_view name = {})
{
    return timestampUs + CameraLatencyUs(source, name);
}

}  // namespace camera_recorder::camera_time
