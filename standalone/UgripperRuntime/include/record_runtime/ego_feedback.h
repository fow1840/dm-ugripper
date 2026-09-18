#pragma once
#include "record_runtime/bms_reader.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace DA::Ugripper {
class EgoSpeechPolicy {
public:
    enum class Phase { Idle, Recording, Writing };
    void SetPhase(Phase phase, uint64_t now) {
        if (phase == phase_) return;
        if (phase == Phase::Writing && !hadRecording_) phase = Phase::Idle;
        phase_ = phase; phaseStart_ = now; phaseNotified_ = false;
        if (phase == Phase::Recording) hadRecording_ = true;
        if (phase == Phase::Writing) hadRecording_ = false;
    }
    void Notify(int code) {
        if ((code == 1204 || code == 1205) && events_.size() < 32 &&
            std::find(events_.begin(), events_.end(), code) == events_.end()) events_.push_back(code);
    }
    std::optional<int> Next(uint64_t now, const std::optional<BatterySample>& battery, bool lowStorage) {
        if (battery) {
            if (battery->charging && battery->percent > 10) notified_.fill(false);
            // A skipped level produces one prompt at the current level, not a burst.
            int level = -1;
            for (int i = 0; i < 6; ++i)
                if (battery->percent <= levels_[i] && !notified_[i]) level = i;
            if (level >= 0 && !battery->charging) {
                for (int i = 0; i <= level; ++i) notified_[i] = true;
                if (events_.size() < 32) events_.push_back(1302);
            }
        }
        if (phase_ == Phase::Recording && now - phaseStart_ <= 3000) return {};
        if (!events_.empty()) {
            const int code = events_.front(); events_.pop_front(); return code;
        }
        if (!phaseNotified_ && now - phaseStart_ > (phase_ == Phase::Writing ? 5000u : 3000u) &&
            (phase_ == Phase::Writing || (phase_ == Phase::Recording && lowStorage))) {
            phaseNotified_ = true;
            if (phase_ == Phase::Writing) return 1003;
            if (lowStorage) return 1203;
        }
        return {};
    }
    Phase GetPhase() const { return phase_; }
private:
    Phase phase_ = Phase::Idle;
    uint64_t phaseStart_ = 0;
    bool phaseNotified_ = false, hadRecording_ = false;
    const std::array<int,6> levels_{10,5,4,3,2,1};
    std::array<bool,6> notified_{};
    std::deque<int> events_;
};
class EgoFeedback {
public:
    using Send = std::function<void(int)>;
    EgoFeedback(std::string disk, Send send);
    ~EgoFeedback();
    void SetPhase(EgoSpeechPolicy::Phase phase);
    void Notify(int code);
private:
    void Run();
    std::string disk_;
    Send send_;
    BmsReader bms_;
    std::mutex mutex_;
    std::condition_variable cv_;
    EgoSpeechPolicy policy_;
    bool stop_ = false;
    std::thread thread_;
};
}
