#pragma once

#include <cstdint>
#include <string_view>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <unistd.h>

namespace ugripper::sensor {

inline constexpr int64_t kEncoderStaleTimeoutMs = 3000;
inline constexpr int kEncoderReadyTimeoutMs = 5000;
inline constexpr int kEncoderInitExit = 32;
inline constexpr int kEncoderStaleLeftExit = 37;
inline constexpr int kEncoderStaleRightExit = 38;

// The child closes the pipe without a token on failure. EOF is never readiness.
inline bool WaitEncoderReady(int fd, int timeoutMs = kEncoderReadyTimeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count());
        pollfd descriptor{fd, POLLIN, 0};
        const int rc = ::poll(&descriptor, 1, std::max(1, remaining));
        if (rc < 0 && errno == EINTR) continue;
        char response = 0;
        return rc > 0 && (descriptor.revents & POLLIN) &&
               ::read(fd, &response, 1) == 1 && response == 'R';
    }
    return false;
}

inline std::string_view EncoderExitErrorType(int exitCode) {
    if (exitCode == kEncoderInitExit) return "encoder_init_failed";
    if (exitCode == kEncoderStaleLeftExit || exitCode == kEncoderStaleRightExit)
        return "encoder_stream_stale";
    return {};
}

class EncoderWatchdog {
public:
    explicit EncoderWatchdog(int64_t startMs) : lastSampleMs_(startMs) {}
    void Observe(int64_t nowMs) { lastSampleMs_ = nowMs; hasSample_ = true; }
    bool Ready() const { return hasSample_; }
    bool Expired(int64_t nowMs) const {
        return nowMs >= lastSampleMs_ && nowMs - lastSampleMs_ >= kEncoderStaleTimeoutMs;
    }
private:
    int64_t lastSampleMs_;
    bool hasSample_ = false;
};

inline bool EncoderCoversTail(uint64_t lastNs, uint64_t referenceNs,
                             uint64_t toleranceNs = 1000000000ULL) {
    return lastNs != 0 && referenceNs != 0 &&
           (lastNs >= referenceNs || referenceNs - lastNs <= toleranceNs);
}

}  // namespace ugripper::sensor
