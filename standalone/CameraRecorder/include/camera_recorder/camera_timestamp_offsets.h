#pragma once

#include <cstdint>
#include <string_view>

namespace camera_recorder::camera_time {

// Signed timestamp offsets for CameraRecorder streams. Positive values move
// timestamps later; negative values compensate observation latency by moving
// timestamps earlier. Update these values when a new experiment is accepted,
// then rebuild.
// Firmware 2026-09-24-1632: episode 20260928_0030 optical calibration,
// shared drift removed: left +3.528 ms / right -0.440 ms under old offsets.
// Both named main cameras now use +7 ms. Invalid episode 0026 is excluded.
// The uncalibrated fallback retains its previous value; no drift compensation.
inline constexpr int64_t kMainCameraLatencyUs = 8'600;
// Tactile now uses the V4L2 capture clock rather than first encoded-output
// observation. The old -35ms included encoding latency and must not carry over.
// Default capture-clock correction for streams without a per-view calibration.
// Clock-rate drift against the external stopwatch is handled offline only.
inline constexpr int64_t kTactileCameraLatencyUs = -2'000;

enum class CameraTimestampSource {
    Main,
    Tactile,
};

// Accepted unified tactile offset from episode 20260928_0030: -2 ms.
// Fixed offsets do not change cadence or compensate clock-rate drift.
constexpr int64_t CameraLatencyUs(CameraTimestampSource source, std::string_view name = {})
{
    switch (source)
    {
    case CameraTimestampSource::Main:
        if (name == "left_cam_main") return 7'000;
        if (name == "right_cam_main") return 7'000;
        return kMainCameraLatencyUs;
    case CameraTimestampSource::Tactile:
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
