#pragma once

#include <cstdint>

namespace fays_stereo_recorder::camera_time {

// Fays stereo camera latency compensation is currently disabled.
// Update this value when a new experiment is accepted, then rebuild.
inline constexpr int64_t kStereoCameraLatencyUs = 0;

constexpr uint64_t AddStereoCameraLatencyNs(uint64_t timestampNs)
{
    return timestampNs + static_cast<uint64_t>(kStereoCameraLatencyUs) * 1'000ULL;
}

}  // namespace fays_stereo_recorder::camera_time
