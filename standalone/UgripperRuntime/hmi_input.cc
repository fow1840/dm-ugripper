#include "record_runtime/hmi_input.h"
#include "utils/ugripper_logger.h"
#include <algorithm>
#include <chrono>
#include <pthread.h>

namespace DA::Ugripper {
const char* HmiInputActionName(HmiInputAction action)
{
    switch (action) {
    case HmiInputAction::RecordUp: return "RecordUp";
    case HmiInputAction::RecordDown: return "RecordDown";
    case HmiInputAction::PreAudio: return "PreAudio";
    case HmiInputAction::PostAudio: return "PostAudio";
    case HmiInputAction::ShutdownPrompt: return "ShutdownPrompt";
    case HmiInputAction::Shutdown: return "Shutdown";
    case HmiInputAction::Unmount: return "Unmount";
    case HmiInputAction::MarkFailed: return "MarkFailed";
    case HmiInputAction::TaskMarker: return "TaskMarker";
    case HmiInputAction::ToggleBinding: return "ToggleBinding";
    }
    return "Unknown";
}

void HmiInputRecognizer::Reset(HmiInputMode mode, uint8_t held, uint64_t now)
{
    mode_ = mode;
    raw_ = held;
    time_ = now;
    release_gate_ = held != 0;
    release_since_.reset();
    chord_latch_ = false;
    click_.reset();
    events_.clear();
    router_.Reset();
    HmiControllerOptions options;
    if (mode == HmiInputMode::Recording) options.up_long_press_threshold_ms = 800;
    right_ = HmiController(options, nullptr);
    left_ = HmiController(HmiControllerOptions{}, nullptr);
}

void HmiInputRecognizer::Emit(HmiInputAction action, uint64_t time)
{
    events_.emplace_back(action, time);
}

void HmiInputRecognizer::Edge(unsigned key, bool pressed, uint64_t time)
{
    if (key >= 4 || time < time_) return;
    // Settle deadlines in the PREVIOUS state before applying the next edge.
    // Otherwise a delayed release would erase a valid but unconfirmed press.
    Advance(time);
    if (pressed) raw_ |= 1u << key;
    else raw_ &= ~(1u << key);
    Tick(time);
}

void HmiInputRecognizer::Advance(uint64_t time)
{
    if (time < time_) return;
    if (mode_ == HmiInputMode::Busy) {
        time_ = time;
        return;
    }
    // Replay receive time at the worker's 5ms cadence, never consumption time.
    while (time_ < time) {
        time_ = std::min(time, time_ + 5);
        Tick(time_);
        if (!raw_ && !click_ && !release_gate_ && !chord_latch_ && time - time_ > 40) {
            // Settle the last release before skipping an arbitrarily long idle.
            for (int i = 0; i < 8; ++i) {
                time_ += 5;
                Tick(time_);
            }
            if (!click_) time_ = time;
        }
    }
}

void HmiInputRecognizer::Tick(uint64_t now)
{
    if (mode_ == HmiInputMode::Busy) return;
    if (release_gate_) {
        if (raw_) release_since_.reset();
        else {
            if (!release_since_) release_since_ = now;
            if (now - *release_since_ >= 40) release_gate_ = false;
        }
        return;
    }
    if (click_ && now - click_->second > 1000) click_.reset();
    const FourButtonSnapshot buttons{bool(raw_ & 1), bool(raw_ & 2), bool(raw_ & 4), bool(raw_ & 8)};
    const auto route = router_.Update(buttons, mode_ == HmiInputMode::Recording, now);
    if (route.consumed_mask) chord_latch_ = true;
    if (chord_latch_) {
        click_.reset();
        right_.Reset();
        left_.Reset();
        for (auto action : route.actions) {
            (void)action;
            Emit(HmiInputAction::ToggleBinding, now);
        }
        // Partial chord release cannot become a single-key action.
        if (!raw_) {
            chord_latch_ = false;
            router_.Reset();
        }
        return;
    }
    for (const auto& event : right_.HandleButtonsAt({buttons.right_up, buttons.right_down}, now)) {
        if (event.type == HmiEventType::ShortUpPressed || event.type == HmiEventType::ShortDownPressed) {
            if (click_ && click_->first == event.type && now - click_->second <= 1000) {
                Emit(event.type == HmiEventType::ShortUpPressed ? HmiInputAction::RecordUp : HmiInputAction::RecordDown, now);
                click_.reset();
            } else {
                click_ = std::pair{event.type, now};
            }
        } else {
            click_.reset();
            switch (event.type) {
            case HmiEventType::LongUpPressed:
                Emit(mode_ == HmiInputMode::Recording ? HmiInputAction::TaskMarker : HmiInputAction::PreAudio, now);
                break;
            case HmiEventType::LongDownPressed: Emit(HmiInputAction::PostAudio, now); break;
            case HmiEventType::ShutdownPromptRequested: Emit(HmiInputAction::ShutdownPrompt, now); break;
            case HmiEventType::ShutdownRequested: Emit(HmiInputAction::Shutdown, now); break;
            default: break;
            }
        }
    }
    for (const auto& event : left_.HandleButtonsAt({buttons.left_up, buttons.left_down}, now)) {
        if (event.type == HmiEventType::ShutdownRequested) Emit(HmiInputAction::Unmount, now);
        else if (event.type == HmiEventType::LongUpPressed && mode_ == HmiInputMode::Idle)
            Emit(HmiInputAction::MarkFailed, now);
    }
}

std::vector<std::pair<HmiInputAction, uint64_t>> HmiInputRecognizer::Take()
{
    auto result = std::move(events_);
    events_.clear();
    return result;
}

uint64_t HmiInputController::Now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

HmiInputController::HmiInputController()
{
    reset_time_ = Now();
    worker_ = std::thread(&HmiInputController::Run, this);
}

HmiInputController::~HmiInputController() { Stop(); }

void HmiInputController::Stop()
{
    { std::lock_guard lock(mutex_); stop_ = true; }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void HmiInputController::ResetLocked(const char* reason)
{
    ++epoch_;
    edges_.clear();
    events_.clear();
    reset_held_ = held_;
    reset_time_ = Now();
    reset_reason_ = reason;
    cv_.notify_one();
}

void HmiInputController::SetMode(HmiInputMode mode)
{
    std::lock_guard lock(mutex_);
    if (mode_ != mode) {
        mode_ = mode;
        ResetLocked("mode");
    }
}

void HmiInputController::Submit(unsigned key, bool pressed)
{
    if (key >= 4) return;
    std::lock_guard lock(mutex_);
    if (stop_) return;
    const auto mask = uint8_t(1u << key);
    if (blocked_keys_ & mask) {
        if (!pressed) blocked_keys_ &= ~mask;
        return;
    }
    if (bool(held_ & mask) == pressed) return;
    if (pressed) held_ |= mask;
    else held_ &= ~mask;
    if (edges_.size() >= 256) {
        ResetLocked("input_overflow");
        return;
    }
    edges_.push_back({key, pressed, Now(), ++sequence_});
    cv_.notify_one();
}

void HmiInputController::LinkReset(bool left)
{
    std::lock_guard lock(mutex_);
    // A missing hand cannot leave the healthy hand permanently gated. Suppress
    // only the disconnected hand's held keys until their real release arrives.
    const uint8_t side = left ? 0x03 : 0x0c;
    blocked_keys_ |= held_ & side;
    held_ &= ~side;
    ResetLocked(left ? "left_link" : "right_link");
}

std::optional<HmiInputEvent> HmiInputController::Pop()
{
    std::lock_guard lock(mutex_);
    if (events_.empty()) return {};
    auto event = events_.front();
    events_.pop_front();
    return event;
}

bool HmiInputController::IsCurrent(const HmiInputEvent& event) const
{
    std::lock_guard lock(mutex_);
    return !stop_ && event.epoch == epoch_ && mode_ != HmiInputMode::Busy;
}

bool HmiInputController::Claim(const HmiInputEvent& event, bool enter_busy)
{
    std::lock_guard lock(mutex_);
    if (stop_ || event.epoch != epoch_ || mode_ == HmiInputMode::Busy || Now() - event.time_ms > 1000) return false;
    if (enter_busy) {
        mode_ = HmiInputMode::Busy;
        ResetLocked("action");
    }
    return true;
}

void HmiInputController::Run()
{
    pthread_setname_np(pthread_self(), "hmi_input");
    uint64_t appliedEpoch = ~uint64_t{0};
    uint64_t consumedSequence = 0;
    uint64_t lastThrough = Now();
    std::unique_lock lock(mutex_);
    while (!stop_) {
        cv_.wait_for(lock, std::chrono::milliseconds(5), [this, &appliedEpoch] {
            return stop_ || !edges_.empty() || appliedEpoch != epoch_;
        });
        if (stop_) break;
        if (Now() - lastThrough > 2000) ResetLocked("worker_late");
        const auto epoch = epoch_;
        const auto mode = mode_;
        const auto resetTime = reset_time_;
        const auto resetHeld = reset_held_;
        const auto reason = reset_reason_;
        auto edges = std::move(edges_);
        edges_.clear();
        // Clock under producer lock: later edges must not precede this watermark.
        const auto through = Now();
        lastThrough = through;
        lock.unlock();
        if (appliedEpoch != epoch) {
            recognizer_.Reset(mode, resetHeld, resetTime);
            appliedEpoch = epoch;
            DM_LOG_INFO("[HMI_INPUT] reset reason={} epoch={} mode={} held={}", reason, epoch, int(mode), unsigned(resetHeld));
        }
        std::vector<HmiInputEvent> recognized;
        auto collect = [&] {
            for (const auto& [action, time] : recognizer_.Take())
                recognized.push_back({action, time, epoch, consumedSequence});
        };
        for (const auto& edge : edges) {
            recognizer_.Advance(edge.time);
            collect();
            consumedSequence = edge.sequence;
            recognizer_.Edge(edge.key, edge.pressed, edge.time);
            collect();
            DM_LOG_INFO("[HMI_INPUT] edge seq={} key={} pressed={} rx_ms={} delay_ms={}", edge.sequence, edge.key, edge.pressed, edge.time, Now() - edge.time);
        }
        recognizer_.Advance(through);
        collect();
        lock.lock();
        if (epoch != epoch_ || stop_) continue;
        if (events_.size() + recognized.size() > 64) {
            ResetLocked("action_overflow");
            continue;
        }
        for (const auto& event : recognized) events_.push_back(event);
        lock.unlock();
        for (const auto& event : recognized)
            DM_LOG_INFO("[HMI_INPUT] recognized action={} seq={} epoch={} input_ms={}",
                HmiInputActionName(event.action), event.sequence, event.epoch, event.time_ms);
        lock.lock();
    }
}
}
