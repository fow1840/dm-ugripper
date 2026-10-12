#pragma once

#include <cstdint>
#include <algorithm>
#include <string_view>

namespace fays_stereo_recorder::camera_time {

// Signed Fays stereo camera timestamp offset. The aligned host timestamp is
// observed after capture, so the accepted latency is compensated by moving the
// camera publishTime earlier. Update this value when a new experiment is
// accepted, then rebuild.
inline constexpr int64_t kStereoCameraLatencyUs = -1'000;

constexpr int64_t StereoCameraLatencyUs(std::string_view videoName = {})
{
    // Right view: first ten valid samples averaged -1ms with the old -1ms
    // correction. Left view has no accepted optical result; keep its offset.
    return videoName == "stereo_right.mkv" ? 0 : kStereoCameraLatencyUs;
}

constexpr uint64_t AddStereoCameraLatencyNs(uint64_t timestampNs, std::string_view videoName = {})
{
    const int64_t offsetNs = StereoCameraLatencyUs(videoName) * 1'000;
    if (offsetNs >= 0)
    {
        return timestampNs + static_cast<uint64_t>(offsetNs);
    }
    const uint64_t advanceNs = static_cast<uint64_t>(-offsetNs);
    return timestampNs >= advanceNs ? timestampNs - advanceNs : 0ULL;
}

// Capture-thread owned. Monotonicity belongs to one recording session, not
// the process lifetime: standby may legitimately rebase to an earlier Unix time.
class SessionTimestampFence {
public:
    uint64_t Apply(uint64_t timestampNs, uint64_t sessionId) {
        const uint64_t result = sessionId && sessionId == sessionId_
            ? std::max(timestampNs, lastNs_ + 1) : timestampNs;
        sessionId_ = sessionId;
        lastNs_ = result;
        return result;
    }
private:
    uint64_t sessionId_ = 0, lastNs_ = 0;
};

}  // namespace fays_stereo_recorder::camera_time
