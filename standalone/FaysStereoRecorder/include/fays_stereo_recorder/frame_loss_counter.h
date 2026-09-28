#pragma once
#include <cstdint>
namespace fays_stereo_recorder {
// Count missing nominal periods between retained frames; never bridge sessions.
// A half-period tolerance avoids interpreting timestamp jitter as a lost frame.
class FrameLossCounter {
public:
    explicit FrameLossCounter(uint64_t periodNs) : periodNs_(periodNs) {}
    uint64_t Observe(uint64_t timestamp) {
        uint64_t missing = 0;
        if (previous_ && timestamp > previous_ && periodNs_) {
            const uint64_t delta = timestamp - previous_;
            const uint64_t periods = delta / periodNs_ +
                (delta % periodNs_ >= (periodNs_ + 1) / 2 ? 1 : 0);
            if (periods > 1) missing = periods - 1;
        }
        previous_ = timestamp;
        total_ += missing;
        if (missing > maxConsecutive_) maxConsecutive_ = missing;
        return missing;
    }
    uint64_t MaxConsecutive() const { return maxConsecutive_; }
    uint64_t Total() const { return total_; }
private:
    uint64_t periodNs_, previous_ = 0, total_ = 0, maxConsecutive_ = 0;
};
}
