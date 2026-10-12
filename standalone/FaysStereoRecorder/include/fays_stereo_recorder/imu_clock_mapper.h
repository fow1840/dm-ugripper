#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace fays_stereo_recorder {
// Single consumer owns this estimator. Published snapshots are synchronized by
// the caller. Raw device timestamps are never rewritten.
class ImuClockMapper {
    struct Track {
        double current = 0, minimum = 0, candidate = 0;
        bool initialized = false;
        bool pending = false, largeStep = false;
        unsigned candidateWindows = 0;
        void Observe(double value) {
            minimum = std::min(minimum, value);
            // Invalidate a large wall-clock step before the next window closes.
            // The window filter, not this individual sample, decides to rebase.
            if (std::abs(value - current) >= 50'000'000) largeStep = true;
        }
        void Begin(double value) {
            current = minimum = value; initialized = true;
            candidateWindows = 0; pending = largeStep = false;
        }
        bool Finish(double dt, bool warming, bool recording, bool wallClock,
                    bool& clockFault) {
            const double residual = minimum - current;
            double target = minimum;
            if (!warming && std::abs(residual) > 2'000'000) {
                pending = true;
                // Consistent lower envelope over five distinct time windows,
                // never five samples from the same delivery burst.
                if (!candidateWindows || std::abs(minimum - candidate) > 500'000) {
                    candidate = minimum; candidateWindows = 1;
                } else {
                    candidate = std::min(candidate, minimum); ++candidateWindows;
                }
                if (candidateWindows < 5) { minimum = std::numeric_limits<double>::infinity(); return false; }
                target = candidate;
            } else { candidateWindows = 0; pending = false; }
            const bool rebase = !warming && !recording && pending;
            if (!warming && recording && wallClock && std::abs(target - current) >= 50'000'000)
                clockFault = true;
            if (warming) current = std::min(current, target);
            else if (rebase) current = target;
            else {
                // Only active sessions slew at 1ms/s. Standby smooths small
                // residuals and rebases confirmed large offsets in one update.
                const double correction = (target - current) * 0.1;
                current += recording ? std::clamp(correction, -dt * 0.001, dt * 0.001)
                                     : correction;
            }
            pending = largeStep = false;
            minimum = std::numeric_limits<double>::infinity();
            return rebase;
        }
    };
public:
    static constexpr uint64_t kWarmupNs = 200'000'000, kStaleNs = 1'000'000'000;
    // Caller serializes this with Observe, after STOP has drained producers
    // and before START opens the recording gate.
    void SetRecording(bool recording) { recording_ = recording; }
    bool Observe(uint64_t device, uint64_t steady, uint64_t wall) {
        if (!device) return false;
        const bool reset = !lastDevice_ || device < lastDevice_;
        const bool resumed = !reset && steady > lastSteady_ + kStaleNs;
        if (device == lastDevice_) return false;
        const double offset = double(steady) - double(device);
        const double wallOffset = double(wall) - double(steady);
        if (reset) {
            if (lastDevice_) ++resets_;
            offset_.Begin(offset); wall_.Begin(wallOffset);
            first_ = windowStart_ = steady; ready_ = false; cold_ = true;
        }
        if (resumed) {
            // A delivery outage is not evidence of a device clock reset.
            // Retain the trusted mapping and reconfirm any new offset normally.
            ++resets_;
            first_ = windowStart_ = steady; ready_ = false; cold_ = false;
            offset_.minimum = wall_.minimum = std::numeric_limits<double>::infinity();
            offset_.candidateWindows = wall_.candidateWindows = 0;
        }
        if (offset_.initialized && offset - offset_.current > 2'000'000) ++delayed_;
        offset_.Observe(offset); wall_.Observe(wallOffset);
        lastDevice_ = device; lastSteady_ = steady;
        if (steady - windowStart_ >= 100'000'000) {
            const bool deviceRebased = offset_.Finish(double(steady - windowStart_), cold_, recording_, false, clockFault_);
            const bool wallRebased = wall_.Finish(double(steady - windowStart_), cold_, recording_, true, clockFault_);
            if (deviceRebased || wallRebased) ++resets_;
            // A positive step at the window boundary can coexist with old
            // samples in its minimum. Keep START blocked until it is resolved.
            if (std::abs(offset - offset_.current) >= 50'000'000) offset_.largeStep = true;
            if (std::abs(wallOffset - wall_.current) >= 50'000'000) wall_.largeStep = true;
            windowStart_ = steady;
            ready_ = steady - first_ >= kWarmupNs;
            if (ready_) cold_ = false;
        }
        return true;
    }
    bool Ready(uint64_t now) const {
        return ready_ && !clockFault_ && now >= lastSteady_ && now - lastSteady_ <= kStaleNs
            && (recording_ || !(offset_.pending || offset_.largeStep || wall_.pending || wall_.largeStep));
    }
    bool WallClockFault() const { return clockFault_; }
    int64_t SteadyOffset() const { return static_cast<int64_t>(offset_.current); }
    int64_t EpochOffset() const { return static_cast<int64_t>(offset_.current + wall_.current); }
    uint64_t Resets() const { return resets_; }
    uint64_t Delayed() const { return delayed_; }
private:
    Track offset_, wall_;
    uint64_t first_ = 0, windowStart_ = 0, lastDevice_ = 0, lastSteady_ = 0;
    uint64_t resets_ = 0, delayed_ = 0;
    bool ready_ = false, cold_ = true, recording_ = false, clockFault_ = false;
};
}
