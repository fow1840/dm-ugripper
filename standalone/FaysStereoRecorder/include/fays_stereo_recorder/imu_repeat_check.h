#pragma once
#include <array>
#include <cstdint>

namespace fays_stereo_recorder {
// Count samples, including the first frame. Constant storage and work per frame.
class ImuRepeatCheck {
public:
    bool Observe(const std::array<double, 6>& axes) {
        if (count_ && axes == previous_) {
            if (count_ >= 1000) return false;
            ++count_;
        } else {
            previous_ = axes;
            count_ = 1;
        }
        return count_ == 1000;
    }
private:
    std::array<double, 6> previous_{};
    uint32_t count_ = 0;
};
}
