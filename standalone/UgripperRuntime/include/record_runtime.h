#ifndef RECORD_RUNTIME_H
#define RECORD_RUNTIME_H

#include "gripper_hmi_driver.h"
#include "gripper_hmi_led_effects.h"
#include "record_runtime/audio_coordinator.h"
#include "record_runtime/ego_feedback.h"
#include "record_runtime/button_action_router.h"
#include "record_runtime/device_state_manager.h"
#include "record_runtime/episode_manager.h"
#include "record_runtime/health_monitor.h"
#include "record_runtime/hmi_controller.h"
#include "record_runtime/process_supervisor.h"
#include "record_runtime/recording_orchestrator.h"
#include "record_runtime/shutdown_request_port.h"
#include "record_runtime/stereo_session_client.h"
#include "record_runtime/usb_recovery_coordinator.h"
#include "utils/fifo_utils.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace DA::Ugripper {

struct RecordRuntimeOptions
{
    std::vector<std::string> gripperPorts;
    std::string cameraRecorderBin = "./bin/CameraRecorder/CameraRecorder";
    std::string sensorRecorderBin = "./bin/SensorRecorder/SensorRecorder";
    std::string faysStereoDaemonScript = "./bin/FaysStereoRecorder/scripts/run_fays_stereo_daemon.sh";
    std::string leftFaysConfig = "./bin/FaysStereoRecorder/config/fays_vikit_left.yaml";
    std::string rightFaysConfig = "./bin/FaysStereoRecorder/config/fays_vikit_right.yaml";
    std::string leftFaysControlFifo = "/dev/shm/ugripper/umi_left_fays_cmd";
    std::string rightFaysControlFifo = "/dev/shm/ugripper/umi_right_fays_cmd";
    std::string audioPlayScript = "./bin/UgripperRuntime/audio/audio_play.py";
    std::string audioRecordScript = "./bin/UgripperRuntime/audio/record_usb_audio.py";
    std::string egoRecordingScript = "./bin/UgripperRuntime/ego/ego_recording_worker.py";
    std::string egoBindingFile = "/dev/shm/ugripper/ego_binding.json";
    std::string firmwareStatusFile = "/dev/shm/ugripper/firmware_status.json";
    std::string egoBindingStatusFile = "/dev/shm/ugripper/ego_binding_status.json";
    std::string audioPipe = "/dev/shm/ugripper/umi_audio_pipe";
    std::string audioReadyFile = "/dev/shm/ugripper/umi_audio_ready";
    std::string audioTempDir = "/tmp/umi_audio";
    std::string recordControlPipe = "/dev/shm/ugripper/umi_record_control.pipe";
    std::string recordingLockFile = "/tmp/umi_recording.lock";
    std::string noiseProfile = "./bin/UgripperRuntime/audio/noise.prof";
    std::string systemActionRequestFile = "/run/ugripper/system_action_request";
    std::string systemActionResultFile = "/run/ugripper/system_action_result";
    std::string restoreUsbCommand = "/usr/local/sbin/ugripper_restore_usb";
    std::string diskRoot = "/mnt/data_disk";
    std::string envFile = "/etc/environment";
    std::string persistCalibrationFile = "/etc/ugripper/config/calibration/calibration.json";
    std::string exampleCalibrationFile = "./calibration.json";
    std::string fallbackCalibrationFile = "./bin/UgripperRuntime/config/fakeCamCalib.json";
    std::string tactileStateDir = "/var/lib/ugripper/tactile_state";
    std::string cameraCodec = "h264";
    std::string stereoControlPipe = "/dev/shm/ugripper/umi_stereo_camera_control.pipe";
    std::string stereoStatusFile = "/tmp/umi_stereo_camera_status.json";
    int pollMs = 20;
};

class RecordRuntime
{
public:
    explicit RecordRuntime(RecordRuntimeOptions options);
    ~RecordRuntime();

    bool Initialize();
    int Run();
    void Stop() noexcept;

private:
    struct ButtonSnapshot
    {
        bool upPressed = false;
        bool downPressed = false;
    };

    enum class LedState
    {
        BootInit,
        Init,
        Writing,
        WaitStorage,
        Ready,
        Warning,
        Recording,
        Error1,
        Error2,
        Error3,
        Error4,
        Error5,
        CalibPre,
        CalibRun,
        CalibDone,
        Exit,
    };

    class GripperPanelManager
    {
    public:
        struct ConnectionEvent
        {
            std::string side;
            bool connected = false;
        };

        struct HealthSnapshot
        {
            bool hasConnectedDevice = false;
            bool inputConnected = false;
            bool inputActive = false;
            uint64_t inputLastRxAgeMs = 0;
            std::vector<std::string> disconnectedPorts;
            std::vector<std::string> inactivePorts;
            std::vector<std::string> inactivePortDetails;
            std::vector<std::string> portActivity;
        };

        bool connect(const std::vector<std::string> &ports);
        void disconnect();
        bool hasConnectedDevice() const;
        bool poll(int timeoutMs, ButtonSnapshot *snapshot);
        bool getButtonsForPortToken(const std::string &token, ButtonSnapshot *snapshot) const;
        HealthSnapshot getHealthSnapshot(uint64_t activeTimeoutMs) const;
        bool requestStateForSide(const std::string &side);
        bool readRuntimeIdentityForSide(const std::string &side,
                                        std::string *serialNumber,
                                        bool *hasSerialNumber,
                                        std::string *errorMessage);
        bool readFirmwareVersionForSide(const std::string &side, std::string *version);
        bool readCalibrationForSide(const std::string &side,
                                    gripper_hmi::GripperCalibrationPayload *payload);
        std::vector<ConnectionEvent> consumeConnectionEvents();
        bool isSideReadyForRefresh(const std::string &side, uint64_t activeTimeoutMs) const;
        bool setBeepStateForSide(const std::string &side, const GripperBeepState &state);
        bool setBeepEnabledForSide(const std::string &side, bool enabled);
        bool silenceBeepForSide(const std::string &side);
        void silenceBeep();
        void setLedEffect(const GripperLedEffect &effect);
        void setLedEffectForSide(const std::string &side, const GripperLedEffect &effect);
        void setLedColor(uint8_t red, uint8_t green, uint8_t blue);
        void setLedColorForSide(const std::string &side, uint8_t red, uint8_t green, uint8_t blue);
        void turnOff();

    private:
        static constexpr size_t kBtnUpKeyIndex = 0;
        static constexpr size_t kBtnDownKeyIndex = 1;
        static constexpr uint64_t kReconnectIntervalMs = 1000;

        static std::string sideForPort(const std::string &port);
        int findDriverIndexForSide(const std::string &side) const;
        bool confirmBeepSilenced(size_t index, const std::string &side);
        void recordConnectionEventIfChanged(size_t index, bool connected);
        void maybeReconnectDriver(size_t index);
        std::vector<std::unique_ptr<GripperHmiDriver>> drivers_;
        std::vector<uint64_t> reconnectAttemptMs_;
        std::vector<uint64_t> delayedStateRequestDueMs_;
        std::vector<bool> lastKnownConnectedStates_;
        std::vector<ConnectionEvent> pendingConnectionEvents_;
        std::vector<GripperBeepState> currentDriverBeepStates_;
        size_t inputDriverIndex_ = 0;
        bool hasDedicatedRightInput_ = false;
        bool hasLedEffect_ = false;
        bool hasDirectLedColor_ = false;
        bool hasBeepState_ = false;
        GripperLedEffect currentLedEffect_{};
        GripperLedColor currentLedColor_{};
        struct PerSideLedState
        {
            bool hasEffect = false;
            bool hasColor = false;
            GripperLedEffect effect{};
            GripperLedColor color{};
        };
        std::vector<PerSideLedState> perSideLedStates_;
        GripperBeepState currentBeepState_{};
    };

    class HmiLedController
    {
    public:
        explicit HmiLedController(GripperPanelManager *panelManager);
        ~HmiLedController();

        void start();
        void stop();
        void setState(LedState state, double progress = 0.0);

    private:
        GripperPanelManager *panelManager_ = nullptr;
        bool hasState_ = false;
        LedState currentState_ = LedState::Exit;
        uint64_t readyBreathPhaseOffsetMs_ = 0;
    };

    using EpisodeManager = DA::Ugripper::EpisodeManager;

    static GripperLedEffect makeLedEffect(LedState state, double progress = 0.0);
    static std::string readEnvValue(const std::string &envFile, const std::string &key);
    static std::string queryPackageVersion(const std::string &packageName);
    static std::string toLower(std::string text);
    static std::string jsonEscape(const std::string &value);
    static uint64_t currentSteadyMs();
    static uint64_t currentEpochMs();
    static int64_t currentEpochUs();
    static bool fileExistsAndNotEmpty(const std::string &path);
    static bool runCommandSync(const std::vector<std::string> &arguments);
    static const char *ledStateName(LedState state);
    static const char *hmiEventName(DA::Ugripper::HmiEventType event_type);

    void handleGripperConnectionEvents();
    void processPendingGripperRefreshes();
    void maintainFirmwareStatus();
    void maintainGripperCalibration();
    void refreshGripperRuntimeStateForSide(const std::string &side,
                                           bool markTactileReferencePending);
    void clearGripperRuntimeStateForSide(const std::string &side,
                                         bool connected,
                                         const std::string &status,
                                         const std::string &errorMessage);
    bool persistGripperCalibrationCache(std::string *errorMessage);
    bool areSideCriticalDevicesReady(const std::string &side) const;
    void initializeMainCameraRuntimeStates();
    void maintainMainCameraRuntimeStates();
    void syncMainCameraRuntimeStatesToEpisodeManager();
    void waitForMainCameraRefreshes();
    void initializeTactileCameraRuntimeStates();
    void maintainTactileCameraRuntimeStates();
    void syncTactileCameraRuntimeStatesToEpisodeManager();
    bool startRecording(bool resetRecording);
    bool stopRecording(bool dueToError, const std::string &reason, const std::string &errorType = "");
    void handleButtons(const ButtonSnapshot &buttons);
    bool handleShortUpAction();
    bool handleShortDownAction();
    bool handlePhysicalRecordShortPress(DA::Ugripper::HmiEventType eventType);
    void clearPendingPhysicalRecordShortPress(const char *reason, uint64_t nowMs);
    bool handleLongUpAction();
    bool handleLongDownAction();
    void handleButtonAction(DA::Ugripper::ButtonAction action);
    void resetLeftButtonTracking();
    void handleRecordingTaskMarker();
    void playHmiFeedback(DA::Ugripper::HmiFeedbackEvent event);
    void handleDualShutdownAction();
    bool handleLeftDualUmountAction();
    bool handleLeftSingleLongMarkFailedAction();
    void handleLeftButtons(const ButtonSnapshot &buttons);
    bool initializeRecordControlPipe();
    void closeRecordControlPipe();
    bool pollRecordControlPipe();
    bool processRecordControlCommand(const std::string &command);
    bool recordAudioClip(const std::string &audioType, bool monitorUpButton);
    bool attachPendingPreAudio(const std::string &episodeDir);
    int maintenanceLockFd_ = -1;
    bool writeRecordingLock(const std::string &episodeDir);
    void removeRecordingLock();
    bool checkRecorderProcesses();
    void monitorHardwareHealth();
    void monitorStereoFrameLoss();
    std::string stereoLossEpisode_;
    uint64_t stereoLossStopDeadlineMs_ = 0;
    bool startAudioPlayer();
    void stopAudioPlayer();
    void maintainAudioPlayer();
    bool startStereoDaemon();
    void stopStereoDaemon();
    void maintainStereoDaemon();
    std::optional<DA::Ugripper::HealthFault> currentHealthFault() const;
    bool isStereoStartupWaiting() const;
    void maintainRestoreUsbRetry();
    void maintainRestoreUsbFailureAlarm();
    void maintainRestoreUsbDevicePresence();
    void suspendStereoDaemonForRestoreUsb(const std::string &reason);
    void resumeStereoDaemonAfterRestoreUsb(const std::string &reason);
    bool isDataDiskMounted() const;
    bool requestDataDiskUmountForRestoreUsb(const std::string &reason);
    bool prepareDataDiskForRestoreUsb(const std::string &reason);
    struct RestoreUsbTriggerContext
    {
        std::string cause;
        std::string evidence;
        std::string side;
    };
    void logRestoreUsbRequested(const RestoreUsbTriggerContext &context);
    bool confirmRestoreUsbTrigger(const RestoreUsbTriggerContext &context);
    bool writeStereoControl(bool recording,
                            const std::string &episodeDir,
                            int64_t startSystemTimeUs,
                            int64_t stopSystemTimeUs);
    bool waitForStereoFinalize(const std::string &episodeDir, int timeoutMs, std::string *errorMessage);
    bool startEgoRecording(const std::string &episodeDir,
                           int64_t startSystemTimeUs,
                           std::string *errorMessage,
                           std::string *errorType);
    bool stopEgoRecording(const std::string &episodeDir, int64_t stopSystemTimeUs, std::string *errorMessage);
    bool waitForEgoFinalize(const std::string &episodeDir,
                            int timeoutMs,
                            std::string *errorMessage,
                            std::string *errorType);
    bool cleanupEgoRemote(const std::string &episodeDir, std::string *errorMessage);
    bool loadEgoBinding();
    bool clearEgoBindingState();
    bool isEgoRequired() const;
    void handleEgoBindingToggle();
    void startEgoFeedback();
    std::optional<DA::Ugripper::HealthFault> detectDataDiskHealthFault(const std::string &path);
    void maintainEgoBindingAction();
    void maintainEgoBindingHealth();
    bool isEgoSyncReady() const;
    bool isEgoSyncPending() const;
    void maintainEgoSyncFeedback();
    void stopEgoSyncFeedback();
    void setEgoBindingFault(const std::string &detail);
    void clearEgoBindingFault();
    bool mergeEpisodeInfo(const std::string &episodeDir, std::string *errorMessage) const;
    bool syncRuntimeLogToDisk(const char *reason) const;
    bool markEpisodeOperatorFailed(const std::string &episodeDir, std::string *errorMessage) const;
    void playOperatorMarkFailedFeedback();
    bool maintainDataStorage();
    bool ensureEpisodeManagerInitialized();
    void markTactileReferencePendingForSide(const std::string &side,
                                            const std::string &reason);
    void scheduleBackgroundTactileValidation(const std::string &episodeDir);
    void maintainBackgroundTactileValidation();
    void maintainTactileBaselineReset();
    void cancelBackgroundTactileValidation();
    void requestStopBackgroundTactileValidation();
    void applyTactileValidationFindings(
        const std::vector<DA::Ugripper::TactileValidationFinding> &findings);
    void applyIdleState();
    void setAudioRecoveryCommand(std::string command);
    void sendAudioCommand(const std::string &command) const;
    void setLedState(LedState state, double progress = 0.0);
    void setTactileWarningLedState();
    void setHardwareFaultLedState(const DA::Ugripper::HealthFault &fault);
    void maybeTriggerRestoreUsbForHardwareFault(const DA::Ugripper::HealthFault &fault);
    std::optional<DA::Ugripper::HealthFault> detectCameraRecorderStartupFailure();
    std::optional<DA::Ugripper::HealthFault> detectCameraRecorderStartupFailureForEpisode(const std::string &episodeDir);
    std::optional<DA::Ugripper::HealthFault> detectCameraRecorderKernelHang();
    void resetCameraRecorderDStateTracking();
    void handleFailureState(DA::Ugripper::RuntimeLedState state,
                            const std::vector<std::string> &errorTypes,
                            const std::string &detail);
    void logRestoreUsbSkipOnce(const std::string &key, const std::string &message);
    void triggerRestoreUsbOnError(const std::string &reason, bool checkMainCameraRecovery = false);
    bool shouldRestoreUsbForFailure(DA::Ugripper::RuntimeLedState state,
                                    const std::vector<std::string> &errorTypes,
                                    const std::string &detail,
                                    bool *checkMainCameraRecovery) const;
    bool shouldDeferRestoreUsbForSide(DA::Ugripper::HardwareFaultSide side,
                                      const std::string &reason) const;
    bool sideHasAnyRestoreUsbDevice(const std::string &side) const;
    bool restoreUsbCriticalSymlinksPresent(std::string *detail) const;
    bool restoreUsbTargetRecovered() const;
    bool mainCameraRestoreTargetsHealthy() const;
    void triggerRestoreUsbFailureAlarm(const std::string &reason);
    void noteRestoreUsbManualInsert(const std::string &side, const std::string &reason);
    void resetRestoreUsbErrorWindow();
    bool isRecordingActive() const;
    const std::string &currentEpisodeDir() const;
    const std::string &lastEpisodeDir() const;
    static LedState toLedState(DA::Ugripper::RuntimeLedState state);

    RecordRuntimeOptions options_;
    std::atomic<bool> stopRequested_{false};
    bool initialized_ = false;
    std::string deviceSn_;
    std::string language_;
    bool chestCameraEnabled_ = true;
    bool stereoEnabled_ = true;
    bool perfLogEnabled_ = false;
    std::string hardwareVersion_;
    std::string packageVersion_;
    std::string updaterVersion_;
    std::string pendingPreAudioFile_;
    std::string audioRecoveryCommand_;
    ButtonSnapshot lastLoggedButtons_{};
    ButtonSnapshot lastLoggedLeftButtons_{};
    bool hasLastLoggedButtons_ = false;
    bool hasLastLoggedLeftButtons_ = false;
    ButtonSnapshot lastLeftButtons_{};
    uint64_t leftBothPressedSinceMs_ = 0;
    bool leftDualChordActive_ = false;
    bool leftDualLongHandled_ = false;
    uint64_t leftSinglePressedSinceMs_ = 0;
    bool leftSingleLongHandled_ = false;
    uint64_t recordingRightUpPressedSinceMs_ = 0;
    bool recordingRightUpHandled_ = false;
    ::utils::BufferedFifoLineReader recordControlReader_{4096};
    uint64_t lastRecordControlActionMs_ = 0;
    bool hasPendingPhysicalRecordShortPress_ = false;
    DA::Ugripper::HmiEventType pendingPhysicalRecordShortPress_ =
        DA::Ugripper::HmiEventType::ShortUpPressed;
    uint64_t pendingPhysicalRecordShortPressMs_ = 0;
    struct TactileWarningSideState
    {
        bool leftSensor = false;
        bool rightSensor = false;
    };
    std::array<TactileWarningSideState, 2> tactileWarningSides_{};
    bool tactileWarningActive_ = false;
    std::string tactileTriggeredAudioCommand_;
    struct BackgroundTactileValidationResult
    {
        std::string episodeDir;
        std::vector<DA::Ugripper::TactileValidationFinding> findings;
    };
    std::mutex backgroundTactileMutex_;
    std::string pendingBackgroundTactileEpisodeDir_;
    std::future<BackgroundTactileValidationResult> backgroundTactileFuture_;
    std::atomic<bool> stopBackgroundTactileValidation_{false};
    std::optional<DA::Ugripper::HealthFault> activeHardwareFault_;
    bool episodeManagerInitialized_ = false;
    bool dataStorageWaitLogged_ = false;
    LedState lastLoggedLedState_ = LedState::Init;
    double lastLoggedLedProgress_ = 0.0;
    bool hasLastLoggedLedState_ = false;
    DA::Ugripper::HealthState healthState_{};
    std::unique_ptr<EpisodeManager> episodeManager_;
    using MainCameraRefreshResult = DA::Ugripper::DeviceStateManager::MainCameraRefreshResult;
    using MainCameraRuntimeCache = DA::Ugripper::DeviceStateManager::MainCameraRuntimeCache;
    using TactileCameraRuntimeCache = DA::Ugripper::DeviceStateManager::TactileCameraRuntimeCache;
    struct GripperFirmwareCache
    {
        std::string version;
        uint64_t checkedAtMs = 0;
        uint64_t nextAttemptMs = 0;
        unsigned attempts = 0;
    };
    std::array<GripperFirmwareCache, 2> gripperFirmwareCache_{};
    uint64_t nextFirmwareStatusMs_ = 0;
    DA::Ugripper::DeviceStateManager deviceStateManager_;
    // Compatibility views keep the existing cross-file implementation stable;
    // DeviceStateManager owns the referenced state.
    std::array<DA::Ugripper::GripperRuntimeState, 2> &gripperRuntimeStates_ =
        deviceStateManager_.GetState().gripperRuntimeStates;
    std::array<bool, 2> &pendingGripperRefresh_ =
        deviceStateManager_.GetState().pendingGripperRefresh;
    std::vector<MainCameraRuntimeCache> &mainCameraRuntimeStates_ =
        deviceStateManager_.GetState().mainCameraRuntimeStates;
    std::vector<TactileCameraRuntimeCache> &tactileCameraRuntimeStates_ =
        deviceStateManager_.GetState().tactileCameraRuntimeStates;
    DA::Ugripper::UsbRecoveryCoordinator usbRecoveryCoordinator_;
    // Compatibility views preserve the current recovery sequencing and detached
    // worker captures; UsbRecoveryCoordinator owns the referenced state.
    std::shared_ptr<std::atomic<bool>> &restoreUsbInProgress_ =
        usbRecoveryCoordinator_.GetState().inProgress;
    std::shared_ptr<std::atomic<uint64_t>> &restoreUsbLastFinishMs_ =
        usbRecoveryCoordinator_.GetState().lastFinishMs;
    int &restoreUsbAttemptsInCurrentError_ =
        usbRecoveryCoordinator_.GetState().attemptsInCurrentError;
    bool &restoreUsbPendingRetryCheck_ =
        usbRecoveryCoordinator_.GetState().pendingRetryCheck;
    bool &restoreUsbSymlinkCheckPassed_ =
        usbRecoveryCoordinator_.GetState().symlinkCheckPassed;
    uint64_t &restoreUsbSymlinkPassedMs_ =
        usbRecoveryCoordinator_.GetState().symlinkPassedMs;
    uint64_t &restoreUsbLastSymlinkProbeMs_ =
        usbRecoveryCoordinator_.GetState().lastSymlinkProbeMs;
    uint64_t &restoreUsbLastMissingLogMs_ =
        usbRecoveryCoordinator_.GetState().lastMissingLogMs;
    std::string &restoreUsbLastMissingDetail_ =
        usbRecoveryCoordinator_.GetState().lastMissingDetail;
    bool &restoreUsbCheckMainCameraRecovery_ =
        usbRecoveryCoordinator_.GetState().checkMainCameraRecovery;
    bool &restoreUsbFailureAlarmActive_ =
        usbRecoveryCoordinator_.GetState().failureAlarmActive;
    uint64_t &restoreUsbFailureAlarmStartMs_ =
        usbRecoveryCoordinator_.GetState().failureAlarmStartMs;
    uint64_t &restoreUsbPreflightFailureUntilMs_ =
        usbRecoveryCoordinator_.GetState().preflightFailureUntilMs;
    bool &restoreUsbStereoDaemonStopped_ =
        usbRecoveryCoordinator_.GetState().stereoDaemonStopped;
    int &cameraRecorderDStatePid_ =
        usbRecoveryCoordinator_.GetState().cameraRecorderDStatePid;
    std::string &cameraRecorderDStateTaskId_ =
        usbRecoveryCoordinator_.GetState().cameraRecorderDStateTaskId;
    uint64_t &cameraRecorderDStateFirstSeenMs_ =
        usbRecoveryCoordinator_.GetState().cameraRecorderDStateFirstSeenMs;
    uint64_t &cameraRecorderDStateLastLogMs_ =
        usbRecoveryCoordinator_.GetState().cameraRecorderDStateLastLogMs;
    uint64_t restoreUsbReconnectSuppressUntilMs_ = 0;
    std::array<bool, 2> &restoreUsbSidePresent_ =
        usbRecoveryCoordinator_.GetState().sidePresent;
    std::array<uint64_t, 2> &restoreUsbInsertGraceUntilMs_ =
        usbRecoveryCoordinator_.GetState().insertGraceUntilMs;
    std::string &restoreUsbPendingTriggerKey_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerKey;
    std::string &restoreUsbPendingTriggerCause_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerCause;
    std::string &restoreUsbPendingTriggerEvidence_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerEvidence;
    std::string &restoreUsbPendingTriggerSide_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerSide;
    uint64_t &restoreUsbPendingTriggerFirstSeenMs_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerFirstSeenMs;
    uint64_t &restoreUsbPendingTriggerLastSeenMs_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerLastSeenMs;
    bool &restoreUsbPendingTriggerLogged_ =
        usbRecoveryCoordinator_.GetState().pendingTriggerLogged;
    std::string &restoreUsbLastSkipLogKey_ =
        usbRecoveryCoordinator_.GetState().lastSkipLogKey;
    std::string &restoreUsbLastReason_ =
        usbRecoveryCoordinator_.GetState().lastReason;
    GripperPanelManager panelManager_;
    std::unique_ptr<HmiLedController> ledController_;
    mutable DA::Ugripper::ProcessSupervisor processSupervisor_{};
    std::unique_ptr<DA::Ugripper::HmiController> hmiController_;
    std::unique_ptr<DA::Ugripper::ButtonActionRouter> buttonActionRouter_;
    DA::Ugripper::TaskMarkerTracker taskMarkerTracker_;
    std::unique_ptr<DA::Ugripper::HealthMonitor> healthMonitor_;
    std::unique_ptr<DA::Ugripper::AudioCoordinator> audioCoordinator_;
    std::unique_ptr<DA::Ugripper::StereoSessionClient> stereoSessionClient_;
    std::unique_ptr<DA::Ugripper::ShutdownRequestPort> shutdownRequestPort_;
    std::optional<DA::Ugripper::SubprocessHandle> egoRecordingWorker_;
    bool egoRecordingAttempted_ = false;
    std::string egoBoundSerial_;
    bool egoBoundConnected_ = true;
    bool egoSyncReady_ = false;
    bool egoSyncWaiting_ = false;
    uint64_t egoSyncFeedbackStartMs_ = 0;
    uint64_t egoSyncLastCheckMs_ = 0;
    uint16_t egoSyncToneHz_ = 0;
    bool egoSyncToneOwned_ = false;
    bool egoSyncSlowLogged_ = false;
    bool egoBindingFaultActive_ = false;
    int egoProbeFailureCount_ = 0;
    uint64_t egoLastProbeStartMs_ = 0;
    std::string egoProbeSerial_;
    std::future<bool> egoBindingActionFuture_;
    std::future<bool> egoProbeFuture_;
    std::unique_ptr<EgoFeedback> egoFeedback_;
    std::unique_ptr<DA::Ugripper::RecordingOrchestrator> recordingOrchestrator_;
};

}  // namespace DA::Ugripper

#endif
