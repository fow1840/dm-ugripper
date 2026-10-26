#pragma once
#include <algorithm>
#include <cstdint>

namespace DA::utils {
// Single-owner, O(1) state. No clock reads, allocations, locks or I/O per sample.
class StreamLossMonitor {
public:
    struct Event { bool warning = false; bool error = false; };
    void Observe(uint64_t timestamp_ns, uint64_t missing = 0, bool retained = true) {
        if (!started_) { started_ = true; first_ = timestamp_ns; }
        last_ = timestamp_ns;
        received_ += retained ? 1 : 0;
        missing_ += missing;
        consecutive_ += missing;
        max_consecutive_ = std::max(max_consecutive_, consecutive_);
        if (retained) consecutive_ = 0;
    }
    void ObservePeriodic(uint64_t timestamp_ns, uint64_t period_ns) {
        uint64_t missing = 0;
        if (started_ && timestamp_ns > last_ && period_ns) {
            const auto dt = timestamp_ns - last_;
            const auto periods = dt / period_ns + (dt % period_ns >= (period_ns + 1) / 2);
            if (periods > 1) missing = periods - 1;
        }
        Observe(timestamp_ns, missing);
    }
    // For host-batched streams: whole-span deficit, not rounded per-arrival gaps.
    // A batch may catch up; only the current deficit is used for the rate.
    void ObserveCountSpan(uint64_t timestamp_ns, uint64_t period_ns, bool track_gaps = false) {
        if (track_gaps) ObservePeriodic(timestamp_ns, period_ns);
        else Observe(timestamp_ns);
        if (timestamp_ns >= first_ && period_ns) {
            const uint64_t expected = (timestamp_ns - first_) / period_ns + 1;
            missing_ = expected > received_ ? expected - received_ : 0;
        }
    }
    // Only a consecutive gap can stop a live stream. UINT64_MAX disables it
    // for host-batched sensors; their existing stale-stream checks still apply.
    Event Check(bool, uint64_t max_allowed_consecutive = UINT64_MAX) {
        Event event;
        if (!errored_ && max_consecutive_ > max_allowed_consecutive) {
            errored_ = true;
            event.error = true;
        }
        return event;
    }
    bool FinalRateExceeded() const {
        return static_cast<long double>(missing_) * 50 >
               static_cast<long double>(received_) + missing_;
    }
    uint64_t MaxConsecutive() const { return max_consecutive_; }
    uint64_t Received() const { return received_; }
    uint64_t Missing() const { return missing_; }
    bool Failed() const { return errored_; }
    bool Warned() const { return warned_; }
    double Rate() const { return received_ + missing_ ? double(missing_) / double(received_ + missing_) : 0; }
private:
    uint64_t first_ = 0, last_ = 0, received_ = 0, missing_ = 0;
    uint64_t consecutive_ = 0, max_consecutive_ = 0;
    bool started_ = false, warned_ = false, errored_ = false;
};
}
