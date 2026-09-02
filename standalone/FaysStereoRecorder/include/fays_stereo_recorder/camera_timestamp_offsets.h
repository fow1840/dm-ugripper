#pragma once

#include <cstdint>

namespace fays_stereo_recorder::camera_time {

// Signed Fays stereo camera timestamp offset. The aligned host timestamp is
// observed after capture, so the accepted latency is compensated by moving the
// camera publishTime earlier. Update this value when a new experiment is
// accepted, then rebuild.
inline constexpr int64_t kStereoCameraLatencyUs = -2'000;

constexpr uint64_t AddStereoCameraLatencyNs(uint64_t timestampNs)
{
    constexpr int64_t offsetNs = kStereoCameraLatencyUs * 1'000;
    if (offsetNs >= 0)
    {
        return timestampNs + static_cast<uint64_t>(offsetNs);
    }
    const uint64_t advanceNs = static_cast<uint64_t>(-offsetNs);
    return timestampNs >= advanceNs ? timestampNs - advanceNs : 0ULL;
}

}  // namespace fays_stereo_recorder::camera_time
