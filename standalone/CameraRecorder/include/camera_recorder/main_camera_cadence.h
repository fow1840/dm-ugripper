#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <optional>

namespace ugripper::camera {
// Firmware 50 Hz -> 60 fps duplicate insertion: a late duplicate, an almost
// adjacent real frame, then a second delayed real frame. Reject missing-frame
// gaps; never use frame index to force an entire recording onto a fixed grid.
// Four ORIGINAL encoder timestamps preceding the candidate provide three
// independent intervals. Never feed corrected PTS back into this gate.
inline bool HasMainCamera50HzStableLeadIn(const std::array<int64_t, 4>& preceding_us)
{
    for (std::size_t i = 1; i < preceding_us.size(); ++i) {
        const auto interval = preceding_us[i] - preceding_us[i - 1];
        if (interval < 17'000 || interval > 23'000) return false;
    }
    return true;
}

inline bool IsMainCamera50HzDuplicate(const std::array<int64_t, 4>& encoder_us)
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
inline std::array<int64_t, 3> MainCamera50HzFrameTimes(int64_t previous_real_us)
{
    return {previous_real_us + 1, previous_real_us + 20'000,
            previous_real_us + 40'000};
}
// Correct cadence in the encoder clock, then map EACH frame with its own
// SEI-selected Tset/Pset. A 10-second anchor change must not be replaced by the
// previous frame's offset, even when it falls inside the three-frame window.
inline std::array<int64_t, 3> MainCamera50HzMappedFrameTimes(
    const std::array<int64_t, 4>& encoder_us,
    const std::array<int64_t, 4>& unix_us)
{
    auto corrected = MainCamera50HzFrameTimes(encoder_us[0]);
    for (std::size_t i = 0; i < corrected.size(); ++i) {
        corrected[i] += unix_us[i + 1] - encoder_us[i + 1];
    }
    return corrected;
}
}  // namespace ugripper::camera

namespace ugripper::camera {
class MainCameraCadenceTracker {
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
        const bool signature=IsMainCamera50HzDuplicate(encoder);
        const bool nearby=next_duplicate_ &&
            (index+1==*next_duplicate_ || index==*next_duplicate_+1) && signature && b<8'000;
        const bool expected=next_duplicate_ && index==*next_duplicate_;
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
