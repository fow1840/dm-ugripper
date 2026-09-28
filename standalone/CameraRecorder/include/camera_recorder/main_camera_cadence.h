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

inline bool IsMainCameraH26450HzDuplicate(const std::array<int64_t, 4>& encoder_us)
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
// A delayed real picture can precede the repeated P picture. In this mode
// the long interval belongs to the REAL picture, not to the duplicate. Keep
// both pictures pending so the delayed real timestamp is not used as an anchor.
inline bool IsMainCamera50HzDelayedPair(
    const std::array<int64_t,5>& encoder_us,
    bool continuous,
    const std::array<bool,4>& intra,
    const std::array<std::size_t,4>& vcl_bytes)
{
    if (!continuous || intra[1] ||
        std::any_of(vcl_bytes.begin(), vcl_bytes.end(), [](std::size_t v) { return v == 0; }))
        return false;
    // Compare against the preceding real P picture and at least one following
    // P picture. An I picture is never a compressed-size reference.
    std::size_t reference = intra[0] ? std::min(vcl_bytes[2], vcl_bytes[3]) : vcl_bytes[0];
    // A delayed I picture is allowed only with two following P references.
    if (intra[0] && (intra[2] || intra[3])) return false;
    bool following_reference = false;
    for (std::size_t i = 2; i < intra.size(); ++i) {
        if (!intra[i]) {
            reference = std::min(reference,vcl_bytes[i]);
            following_reference = true;
        }
    }
    if (!following_reference) return false;
    const auto lead = encoder_us[1] - encoder_us[0];
    const auto repeat = encoder_us[2] - encoder_us[1];
    const auto next = encoder_us[3] - encoder_us[2];
    const auto recovery = encoder_us[4] - encoder_us[3];
    const auto span = encoder_us[4] - encoder_us[0];
    // Require the whole delayed-real/short-repeat/recovery sequence, with
    // independent picture evidence on BOTH sides. No phase-only acceptance.
    return lead >= 26'000 && lead <= 36'000 &&
        repeat >= 6'000 && repeat <= 14'000 && next > 0 && next <= 10'000 &&
        recovery >= 8'000 && recovery <= 22'000 && span >= 50'000 && span <= 70'000 &&
        20ULL * vcl_bytes[1] < 7ULL * reference;
}

inline std::array<int64_t,4> MainCamera50HzDelayedPairMappedTimes(
    const std::array<int64_t,5>& encoder_us,
    const std::array<int64_t,5>& unix_us)
{
    std::array<int64_t,4> corrected = {20'000,20'001,40'000,60'000};
    for (std::size_t i = 0; i < corrected.size(); ++i)
        corrected[i] += encoder_us[0] + unix_us[i+1] - encoder_us[i+1];
    return corrected;
}

}  // namespace ugripper::camera

namespace ugripper::camera {
class MainCameraH264CadenceTracker {
    std::optional<uint64_t> next_duplicate_;
public:
    void Reset() { next_duplicate_.reset(); }
    bool tracked = false;
    bool ShouldCorrect(uint64_t index, const std::array<int64_t,4>& encoder,
                       bool stable_lead_in, bool continuous, bool previous_intra,
                       bool current_intra = false,
                       const std::array<std::size_t,3>& vcl_bytes = {},
                       const std::array<bool,2>& following_intra = {}) {
        tracked = false;
        if (!continuous) { Reset(); return false; }
        if (next_duplicate_ && index > *next_duplicate_ + 1) Reset();
        const auto a=encoder[1]-encoder[0], b=encoder[2]-encoder[1], c=encoder[3]-encoder[2];
        const bool signature=IsMainCameraH26450HzDuplicate(encoder);
        const bool nearby=next_duplicate_ &&
            (index+1==*next_duplicate_ || index==*next_duplicate_+1) && signature && b<8'000;
        const bool expected=next_duplicate_ && index==*next_duplicate_;
        // A rejected I candidate may be followed by a repeat one frame later.
        const bool post_intra_phase = expected || (next_duplicate_ &&
            (index+1==*next_duplicate_ || index==*next_duplicate_+1) && std::min(b,c)<8'000);
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
        // A nearby I picture makes compressed sizes incomparable. Do not
        // acquire or sustain a phase using that window. One unmodified frame
        // is safer than shifting a real exposure onto the preceding one.
        const bool competing_next = sizes_known &&
            10ULL*vcl_bytes[1] < 7ULL*std::min(vcl_bytes[0],vcl_bytes[2]);
        if (competing_next && !following_intra[0]) {
            // A strong next picture is expected when we are exactly one frame
            // BEFORE the prediction. Keep that evidence; only a conflicting
            // prediction must be discarded.
            if (!next_duplicate_ || index + 1 != *next_duplicate_) Reset();
            return false;
        }
        if (following_intra[0] || following_intra[1]) {
            // Never acquire from an I/P size contrast. An existing prediction
            // may survive when timing and an independent following P picture
            // both support it. Ignore I sizes rather than discarding the whole
            // boundary (which loses valid repeats in otherwise stable streams).
            std::size_t reference = 0;
            for (std::size_t j = 0; j < following_intra.size(); ++j) {
                if (!following_intra[j] && vcl_bytes[j+1] != 0)
                    reference = reference == 0 ? vcl_bytes[j+1] : std::min(reference,vcl_bytes[j+1]);
            }
            if (expected && !current_intra && signature && vcl_bytes[0] != 0 &&
                reference != 0 && 10ULL*vcl_bytes[0] < 7ULL*reference) {
                next_duplicate_ = index + 6;
                tracked = true;
                return true;
            }
            if (expected) Reset();
            return false;
        }
        // Prediction is corroboration, never an independent correction gate.
        // In particular, a non-compact real frame must not keep a bad phase
        // alive merely because it is six frames after the previous mistake.
        const bool picture_support = !sizes_known || compact;
        tracked=continuous && expected && signature && picture_support;
        const bool regular = !current_intra && (
            (compact && signature) || after_intra ||
            // Locked phase and timing signature survive post-I residual size.
            (previous_intra && post_intra_phase && signature) ||
            (!previous_intra && plausible &&
                (tracked || (picture_support && ((continuous && nearby) || (stable_lead_in && signature))))));
        if(repeated_intra || regular) {
            next_duplicate_=index+6;
            return true;
        }
        // Retain only a one-frame I-boundary grace period. A rejected normal
        // predicted picture ends the phase immediately; it never advances it.
        if (expected && !current_intra) Reset();
        return false;
    }
};
} // namespace ugripper::camera
