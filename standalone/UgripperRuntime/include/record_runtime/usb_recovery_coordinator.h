#ifndef UGRIPPER_RUNTIME_USB_RECOVERY_COORDINATOR_H
#define UGRIPPER_RUNTIME_USB_RECOVERY_COORDINATOR_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace ugripper::runtime
{

class UsbRecoveryCoordinator
{
public:
    struct State
    {
        std::shared_ptr<std::atomic<bool>> inProgress =
            std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<uint64_t>> lastFinishMs =
            std::make_shared<std::atomic<uint64_t>>(0);
        int attemptsInCurrentError = 0;
        bool pendingRetryCheck = false;
        bool symlinkCheckPassed = false;
        uint64_t symlinkPassedMs = 0;
        uint64_t lastSymlinkProbeMs = 0;
        uint64_t lastMissingLogMs = 0;
        std::string lastMissingDetail;
        bool checkMainCameraRecovery = false;
        bool failureAlarmActive = false;
        uint64_t failureAlarmStartMs = 0;
        uint64_t preflightFailureUntilMs = 0;
        bool stereoDaemonStopped = false;
        int cameraRecorderDStatePid = -1;
        std::string cameraRecorderDStateTaskId;
        uint64_t cameraRecorderDStateFirstSeenMs = 0;
        uint64_t cameraRecorderDStateLastLogMs = 0;
        std::array<bool, 2> sidePresent{};
        std::array<uint64_t, 2> insertGraceUntilMs{};
        std::string pendingTriggerKey;
        std::string pendingTriggerCause;
        std::string pendingTriggerEvidence;
        std::string pendingTriggerSide;
        uint64_t pendingTriggerFirstSeenMs = 0;
        uint64_t pendingTriggerLastSeenMs = 0;
        bool pendingTriggerLogged = false;
        std::string lastSkipLogKey;
        std::string lastReason;
    };

    State &GetState() noexcept
    {
        return state_;
    }

    const State &GetState() const noexcept
    {
        return state_;
    }

private:
    State state_;
};

} // namespace ugripper::runtime

#endif
