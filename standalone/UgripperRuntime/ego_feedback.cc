#include <algorithm>
#include "record_runtime/ego_feedback.h"
#include "utils/ugripper_logger.h"
#include <filesystem>

namespace DA::Ugripper {
EgoFeedback::EgoFeedback(std::string disk, Send send)
    : disk_(std::move(disk)), send_(std::move(send)), thread_(&EgoFeedback::Run, this) {}
EgoFeedback::~EgoFeedback() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}
void EgoFeedback::SetPhase(EgoSpeechPolicy::Phase phase) {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_.SetPhase(phase, BatterySteadyMs());
}
void EgoFeedback::Notify(int code) {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_.Notify(code);
}
void EgoFeedback::Run() {
    uint64_t nextStorageCheck = 0;
    bool low = false;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        const bool recording = policy_.GetPhase() == EgoSpeechPolicy::Phase::Recording;
        lock.unlock();
        const auto battery = bms_.Sample();
        const auto now = BatterySteadyMs();
        if (!recording) { low = false; nextStorageCheck = 0; }
        else if (now >= nextStorageCheck) {
            std::error_code error;
            const auto space = std::filesystem::space(disk_, error);
            low = !error && space.capacity > 0 &&
                static_cast<long double>(space.available) / space.capacity < 0.05L;
            nextStorageCheck = now + 1000;
        }
        lock.lock();
        if (stop_) break;
        const auto code = policy_.Next(BatterySteadyMs(), battery, low);
        if (code) {
            lock.unlock();
            try { send_(*code); }
            catch (const std::exception& e) { DM_LOG_WARN("ego speech {} failed: {}", *code, e.what()); }
            lock.lock();
        }
        cv_.wait_for(lock, std::chrono::milliseconds(100), [&] { return stop_; });
    }
}
}
