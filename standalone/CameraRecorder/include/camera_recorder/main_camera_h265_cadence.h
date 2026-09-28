#pragma once
#include "camera_recorder/main_camera_cadence.h"

namespace ugripper::camera {
inline bool IsMainCameraH26550HzDuplicate(const std::array<int64_t, 4>& encoder_us)
{
    const auto a = encoder_us[1] - encoder_us[0];
    const auto b = encoder_us[2] - encoder_us[1];
    const auto c = encoder_us[3] - encoder_us[2];
    constexpr int64_t period_us = 20'000;
    // Broad relative bounds allow encoder jitter. Require the whole signature,
    // not a single long interval or a rigid every-sixth-frame counter.
    const auto short_gap = std::min(b, c);
    const auto recovery_gap = std::max(b, c);
    // Encoder stalls can move the short interval from the first real frame to
    // the second. Treat the following pair as a window, not a fixed ordering.
    return a >= period_us * 95 / 100 && a <= period_us * 18 / 10 &&
           short_gap > 0 && short_gap <= period_us * 6 / 10 &&
           2 * short_gap < a && recovery_gap <= period_us * 11 / 10 &&
           a + short_gap >= period_us && a + short_gap <= period_us * 2 &&
           a + b + c >= period_us * 16 / 10 &&
           a + b + c <= period_us * 25 / 10;
}
// H265 retains its existing policy; H264 field patterns must not retune it.
class MainCameraH265CadenceTracker {
    std::optional<uint64_t> next_duplicate_;
public:
    void Reset() { next_duplicate_.reset(); }
    bool tracked = false;
    bool ShouldCorrect(uint64_t index, const std::array<int64_t,4>& encoder,
                       bool stable_lead_in, bool continuous, bool previous_intra,
                       bool current_intra = false,
                       const std::array<std::size_t,3>& vcl_bytes = {}) {
        tracked = false;
        if (!continuous) { Reset(); return false; }
        if (next_duplicate_ && index > *next_duplicate_ + 1) Reset();
        const auto a=encoder[1]-encoder[0], b=encoder[2]-encoder[1], c=encoder[3]-encoder[2];
        const bool signature=IsMainCameraH26550HzDuplicate(encoder);
        const bool nearby=next_duplicate_ &&
            (index+1==*next_duplicate_ || index==*next_duplicate_+1) && signature && b<8'000;
        const bool expected=next_duplicate_ && index==*next_duplicate_;
        // +5 also covers one frame after an expected I candidate which was
        // rejected: its prediction has already advanced by six.
        const bool post_intra_phase = expected || (next_duplicate_ &&
            (index+1==*next_duplicate_ || index==*next_duplicate_+1 ||
             index+5==*next_duplicate_) && std::min(b,c)<8'000);
        if(expected) next_duplicate_=index+6;
        const bool plausible=std::min({a,b,c})>0 && std::max({a,b,c})<40'000 &&
            a+b+c>=32'000 && a+b+c<=50'000;
        // Relative compressed-picture evidence distinguishes a repeated P
        // picture after an I-frame from an I-frame which itself repeats input.
        // This is corroborating evidence, not a universal codec guarantee.
        const bool sizes_known = vcl_bytes[0] && vcl_bytes[1] && vcl_bytes[2];
        const bool compact = sizes_known &&
            10ULL*vcl_bytes[0] < 7ULL*std::min(vcl_bytes[1],vcl_bytes[2]);
        const bool next_compact = sizes_known && 10ULL*vcl_bytes[1] < 7ULL*vcl_bytes[2];
        if(std::min({a,b,c})<=0 || std::max({a,b,c})>=40'000) { Reset(); return false; }
        const bool after_intra = previous_intra && !current_intra && compact &&
            b<12'000 && c>5'000 && c<24'000;
        const bool repeated_intra = current_intra && sizes_known && !next_compact &&
            plausible && signature && (expected || stable_lead_in);
        tracked=continuous && expected && a>=16'000;
        const bool regular = !current_intra && (
            (compact && signature) || after_intra ||
            // Locked phase and timing signature survive post-I residual size.
            (previous_intra && post_intra_phase && signature) ||
            (!previous_intra && plausible &&
                (tracked || (continuous && nearby) || (stable_lead_in && signature))));
        if(repeated_intra || regular) {
            next_duplicate_=index+6;
            return true;
        }
        if(expected && !plausible && !current_intra && !previous_intra) Reset();
        return false;
    }
};
} // namespace ugripper::camera
