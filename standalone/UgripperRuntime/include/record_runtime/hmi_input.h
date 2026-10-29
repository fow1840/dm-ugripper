#pragma once
#include "record_runtime/hmi_controller.h"
#include "record_runtime/button_action_router.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace DA::Ugripper {
enum class HmiInputMode { Busy, Idle, Recording };
enum class HmiInputAction {
    RecordUp, RecordDown, PreAudio, PostAudio, ShutdownPrompt, Shutdown,
    Unmount, MarkFailed, TaskMarker, ToggleBinding
};
const char* HmiInputActionName(HmiInputAction action);
struct HmiInputEvent {
    HmiInputAction action;
    uint64_t time_ms;
    uint64_t epoch;
    uint64_t sequence;
};

// Pure state machine: timestamped edges and timer advancement, no I/O.
class HmiInputRecognizer {
public:
    void Reset(HmiInputMode mode, uint8_t held_mask, uint64_t now);
    void Edge(unsigned key, bool pressed, uint64_t time);
    void Advance(uint64_t time);
    std::vector<std::pair<HmiInputAction, uint64_t>> Take();
private:
    void Tick(uint64_t time);
    void Emit(HmiInputAction action, uint64_t time);
    HmiInputMode mode_ = HmiInputMode::Busy;
    uint8_t raw_ = 0;
    uint64_t time_ = 0;
    bool release_gate_ = false;
    std::optional<uint64_t> release_since_;
    bool chord_latch_ = false;
    HmiController right_{HmiControllerOptions{}, nullptr};
    HmiController left_{HmiControllerOptions{}, nullptr};
    ButtonActionRouter router_{{{ButtonAction::ToggleEgoBinding,
        ButtonMask(PhysicalButton::LeftUp) | ButtonMask(PhysicalButton::RightUp), 2000, false}}};
    std::optional<std::pair<HmiEventType, uint64_t>> click_;
    std::vector<std::pair<HmiInputAction, uint64_t>> events_;
};

// One worker for both hands. Transport enqueues edges; Runtime claims actions.
class HmiInputController {
public:
    HmiInputController();
    ~HmiInputController();
    void Stop();
    void Submit(unsigned key, bool pressed);
    void LinkReset(bool left);
    void SetMode(HmiInputMode mode);
    std::optional<HmiInputEvent> Pop();
    bool IsCurrent(const HmiInputEvent& event) const;
    bool Claim(const HmiInputEvent& event, bool enter_busy);
    static uint64_t Now();
private:
    struct Edge { unsigned key; bool pressed; uint64_t time; uint64_t sequence; };
    void ResetLocked(const char* reason);
    void Run();
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    HmiInputMode mode_ = HmiInputMode::Busy;
    uint8_t held_ = 0;
    uint8_t blocked_keys_ = 0; // held across a link loss; rearm on real release
    uint64_t epoch_ = 0;
    uint64_t sequence_ = 0;
    uint64_t reset_time_ = 0;
    uint8_t reset_held_ = 0;
    const char* reset_reason_ = "startup";
    std::deque<Edge> edges_;
    std::deque<HmiInputEvent> events_;
    HmiInputRecognizer recognizer_;
    std::thread worker_;
};
}
