#pragma once

#include "camera_recorder/main_camera_xu_rtc.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace ugripper::camera {

// Own compressed bytes while waiting for SEI acknowledgement of the initial SET.
// A new anchor can date earlier frames only within a decodable, continuous run.
class MainCameraStartupBuffer {
public:
    static constexpr int64_t kMaxWaitUs = 1'500'000;
    static constexpr std::size_t kMaxFrames = 180;
    static constexpr std::size_t kMaxBytes = 64 * 1024 * 1024;
    struct Frame {
        std::vector<uint8_t> data;
        MainCameraXuTimestamp timestamp;
        bool valid = false;
        bool clean_keyframe = false;
        bool continuous = false;
        bool intra = false;
        std::size_t vcl_bytes = 0;
        int64_t unix_us = 0;
    };
    enum class Result { Waiting, Ready, Overflow };

    Result Push(Frame frame, uint64_t tset, uint64_t pset, int64_t session_start_us) {
        const bool matches = frame.valid && frame.timestamp.xu_time_us == tset;
        // Never start accepting another unknown anchor after seeing the new one.
        const bool stale_after_ack = acknowledged_ && !matches;
        if (matches) acknowledged_ = true;
        const auto mapped = frame.valid ? MainCameraFrameUnixTimeUs(
            {frame.timestamp.encoder_pts_us, tset}, pset) : std::nullopt;
        if (!mapped || *mapped < session_start_us || stale_after_ack) {
            Discard();
            ++dropped_;
            return Result::Waiting;
        }
        if (!frames_.empty() && (!frame.continuous ||
                frame.timestamp.encoder_pts_us <= frames_.back().timestamp.encoder_pts_us)) {
            Discard();
        }
        if (frames_.empty() && !frame.clean_keyframe) {
            ++dropped_;
            return Result::Waiting;
        }
        if (frames_.size() >= kMaxFrames || frame.data.size() > kMaxBytes - bytes_) {
            Discard();
            ++dropped_;
            return Result::Overflow;
        }
        frame.unix_us = *mapped;
        bytes_ += frame.data.size();
        frames_.push_back(std::move(frame));
        return acknowledged_ ? Result::Ready : Result::Waiting;
    }

    std::deque<Frame> Take() {
        bytes_ = 0;
        return std::exchange(frames_, {});
    }
    void Discard() {
        dropped_ += frames_.size();
        frames_.clear();
        bytes_ = 0;
    }
    std::size_t size() const { return frames_.size(); }
    std::size_t dropped() const { return dropped_; }
private:
    std::deque<Frame> frames_;
    std::size_t bytes_ = 0;
    std::size_t dropped_ = 0;
    bool acknowledged_ = false;
};
}  // namespace ugripper::camera
