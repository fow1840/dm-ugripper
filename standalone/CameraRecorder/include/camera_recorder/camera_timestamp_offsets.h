#pragma once

#include <cstdint>
#include <string_view>

namespace camera_recorder::camera_time {

// Signed timestamp offsets for CameraRecorder streams. Positive values move
// timestamps later; negative values compensate observation latency by moving
// timestamps earlier. Update these values when a new experiment is accepted,
// then rebuild.
// Firmware 2026-09-24-1632: optical exposure-end calibration, episodes
// 20260924_0004..0006. Median of each episode's first ten valid errors:
// left -37.6 ms, right -36 ms. New offset = old offset - measured error.
// No clock-rate drift compensation; the fallback is the two-side mean.
inline constexpr int64_t kMainCameraLatencyUs = 8'600;
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
        if (name == "left_cam_main") return 10'400;
        if (name == "right_cam_main") return 6'800;
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
