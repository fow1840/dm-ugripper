#pragma once

#include <chrono>
#include <cstdint>
#include <map>

namespace fays_stereo_recorder {

class SdkWarningGate {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kNoFrameThreshold = std::chrono::milliseconds(200);
    static constexpr auto kWarnInterval = std::chrono::seconds(2);

    SdkWarningGate(int noNewFrameCode, Clock::time_point startedAt)
        : noNewFrameCode_(noNewFrameCode), lastFrameAt_(startedAt) {}

    void RecordFrame(Clock::time_point now) {
        lastFrameAt_ = now;
        hasFrame_ = true;
        lastWarnings_.erase(noNewFrameCode_);
    }

    bool ShouldWarn(int result, Clock::time_point now) {
        if (result == noNewFrameCode_ && now - lastFrameAt_ < kNoFrameThreshold) {
            return false;
        }
        const auto previous = lastWarnings_.find(result);
        if (previous != lastWarnings_.end() && now - previous->second < kWarnInterval) {
            return false;
        }
        lastWarnings_[result] = now;
        return true;
    }

    bool HasFrame() const { return hasFrame_; }

    int64_t NoFrameMs(Clock::time_point now) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameAt_).count();
    }

private:
    const int noNewFrameCode_;
    Clock::time_point lastFrameAt_;
    bool hasFrame_ = false;
    std::map<int, Clock::time_point> lastWarnings_;
};

}
