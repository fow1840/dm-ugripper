#include "record_runtime/episode_manager.h"
#include "record_runtime/runtime_episode_internal.h"
#include "utils/ugripper_logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

using namespace DA::Ugripper::detail;

void DA::Ugripper::EpisodeManager::ValidateTactileEpisode(
    const std::string &episodeDir,
    std::vector<TactileValidationFinding> *tactileFindings) const
{
    const int64_t tactileValidationStartMs = steadyNowMs();
    const std::string bootId = currentBootId();
    json tactileHistory = json::object();
    {
        std::ifstream historyInput(tactileHistoryPath(tactileStateDir_));
        if (historyInput.is_open())
        {
            try
            {
                historyInput >> tactileHistory;
            }
            catch (const std::exception &ex)
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to parse tactile history, reset in-memory state: "
                                     << ex.what() << std::endl).str());
                tactileHistory = json::object();
            }
        }
    }
    if (!tactileHistory.is_object())
    {
        tactileHistory = json::object();
    }
    if (!tactileHistory.contains("cameras") || !tactileHistory["cameras"].is_object())
    {
        tactileHistory["cameras"] = json::object();
    }

    bool historyDirty = false;
    if (tactileFindings != nullptr)
    {
        tactileFindings->clear();
    }
    for (const auto &target : kTactileCalibrationTargets)
    {
        const std::string serial = cachedTactileSerialForName(target.cameraName);
        if (serial.empty())
        {
            continue;
        }

        std::string frameError;
        const char *fileName = episodeVideoFileNameForCamera(target.cameraName);
        if (fileName == nullptr || *fileName == '\0')
        {
            continue;
        }

        const auto currentFrame = captureTactileGrayFrame(
            {"-i", episodeDir + "/" + std::string(fileName)},
            kTactileEpisodeProbeSec,
            &frameError);
        if (!currentFrame.has_value())
        {
            const json &cameraHistory = tactileHistory["cameras"][serial];
            if (cameraHistory.is_object() &&
                (cameraHistory.value("reference_pending", false) ||
                 cameraHistory.value("persistent_baseline_pending", false) ||
                 cameraHistory.value("persistent_baseline_refresh_on_next_boot", false)))
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile reference deferred: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << frameError
                    << std::endl).str());
            }
            continue;
        }

        json &cameraHistory = tactileHistory["cameras"][serial];
        if (!cameraHistory.is_object())
        {
            cameraHistory = json::object();
        }
        if (!cameraHistory.contains("recent") || !cameraHistory["recent"].is_array())
        {
            cameraHistory["recent"] = json::array();
        }
        if (!cameraHistory.contains("persistent_recent") || !cameraHistory["persistent_recent"].is_array())
        {
            cameraHistory["persistent_recent"] = json::array();
        }
        cameraHistory["camera_name"] = target.cameraName;
        const uint64_t nowMs = dependencies_.currentEpochMs();
        cameraHistory["updated_at_ms"] = nowMs;

        std::vector<uint8_t> baselineFrame;
        std::string baselineError;
        const bool hasReferenceBaseline =
            readBinaryFileExact(tactileReferenceRawPath(tactileStateDir_, serial),
                                kTactileFrameBytes,
                                &baselineFrame,
                                &baselineError);
        const bool referencePending = cameraHistory.value("reference_pending", false);
        bool initializedReferenceFromEpisode = false;
        if (!hasReferenceBaseline || referencePending)
        {
            std::string writeError;
            if (writeBinaryFile(tactileReferenceRawPath(tactileStateDir_, serial),
                                *currentFrame,
                                &writeError) &&
                writeTactileReferenceMeta(tactileStateDir_,
                                          serial,
                                          target,
                                          dependencies_.currentEpochMs(),
                                          &writeError))
            {
                cameraHistory["reference_pending"] = false;
                cameraHistory["reference_pending_reason"] = "";
                cameraHistory["reference_pending_at_ms"] = 0;
                cameraHistory["reference_updated_at_ms"] = dependencies_.currentEpochMs();
                cameraHistory["recent"] = json::array();
                historyDirty = true;
                initializedReferenceFromEpisode = true;
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "tactile reference initialized from episode: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << (referencePending ? " reason=pending" : " reason=missing")
                    << std::endl).str());
            }
            else
            {
                cameraHistory["reference_pending"] = true;
                cameraHistory["reference_pending_reason"] =
                    referencePending ? cameraHistory.value("reference_pending_reason", std::string("write_failed"))
                                     : std::string("missing_reference_write_failed");
                historyDirty = true;
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile reference deferred: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << writeError
                    << std::endl).str());
            }
        }

        const bool persistentWarningActiveBefore =
            cameraHistory.value("persistent_fault_active", false);
        bool persistentWarningActiveAfter = persistentWarningActiveBefore;
        std::string persistentDetail =
            cameraHistory.value("persistent_fault_detail", std::string());

        bool persistentWarningTriggered = false;
        std::vector<uint8_t> persistentBaseline;
        std::string persistentBaselineError;
        const bool hasPersistentBaseline =
            readBinaryFileExact(tactilePersistentRawPath(tactileStateDir_, serial),
                                kTactileFrameBytes,
                                &persistentBaseline,
                                &persistentBaselineError);
        const uint64_t persistentBaselineUpdatedAtMs =
            cameraHistory.value("persistent_baseline_updated_at_ms", static_cast<uint64_t>(0));
        const bool legacyPersistentPending = cameraHistory.value("persistent_baseline_pending", false);
        const bool persistentRefreshOnNextBoot =
            cameraHistory.value("persistent_baseline_refresh_on_next_boot", false);
        const std::string persistentRefreshRequestBootId =
            cameraHistory.value("persistent_baseline_refresh_request_boot_id", std::string());
        const bool persistentRefreshAfterReboot =
            persistentRefreshOnNextBoot &&
            !persistentRefreshRequestBootId.empty() &&
            !bootId.empty() &&
            persistentRefreshRequestBootId != bootId;
        const bool persistentNeedsUpdate =
            !hasPersistentBaseline || persistentBaselineUpdatedAtMs == 0 ||
            persistentRefreshAfterReboot;
        const std::string persistentUpdateReason =
            (!hasPersistentBaseline || persistentBaselineUpdatedAtMs == 0)
                ? "missing"
                : (persistentRefreshAfterReboot ? "alarm_reboot" : "");
        if (legacyPersistentPending && !persistentNeedsUpdate)
        {
            cameraHistory["persistent_baseline_pending"] = false;
            cameraHistory["persistent_baseline_pending_reason"] = "";
            cameraHistory["persistent_baseline_pending_at_ms"] = 0;
            historyDirty = true;
        }
        bool initializedPersistentFromEpisode = false;
        if (persistentNeedsUpdate)
        {
            std::string writeError;
            if (writeBinaryFile(tactilePersistentRawPath(tactileStateDir_, serial),
                                *currentFrame,
                                &writeError))
            {
                cameraHistory["persistent_baseline_pending"] = false;
                cameraHistory["persistent_baseline_pending_reason"] = "";
                cameraHistory["persistent_baseline_pending_at_ms"] = 0;
                cameraHistory["persistent_baseline_updated_at_ms"] = nowMs;
                cameraHistory["persistent_baseline_refresh_on_next_boot"] = false;
                cameraHistory["persistent_baseline_refresh_request_boot_id"] = "";
                cameraHistory["persistent_baseline_refresh_request_at_ms"] = 0;
                cameraHistory["persistent_fault_active"] = false;
                cameraHistory["persistent_fault_detail"] = "";
                cameraHistory["persistent_recent"] = json::array();
                persistentWarningActiveAfter = false;
                persistentDetail.clear();
                historyDirty = true;
                initializedPersistentFromEpisode = true;
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "tactile persistent baseline initialized from episode: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << persistentUpdateReason
                    << std::endl).str());
            }
            else
            {
                cameraHistory["persistent_baseline_pending"] = true;
                cameraHistory["persistent_baseline_pending_reason"] = persistentUpdateReason + "_write_failed";
                cameraHistory["persistent_baseline_pending_at_ms"] = nowMs;
                historyDirty = true;
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile persistent baseline deferred: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << writeError
                    << std::endl).str());
            }
        }

        if (!hasReferenceBaseline || referencePending || initializedReferenceFromEpisode)
        {
            continue;
        }

        const TactileFrameMetrics metrics = computeTactileFrameMetrics(baselineFrame, *currentFrame);
        bool warningActiveBefore = false;
        const json &recentBefore = cameraHistory["recent"];
        if (recentBefore.size() >= kTactileHistoryWindow)
        {
            warningActiveBefore = std::all_of(
                recentBefore.begin(),
                recentBefore.end(),
                [](const json &entry) { return entry.is_boolean() && entry.get<bool>(); });
        }

        json &recent = cameraHistory["recent"];
        recent.push_back(metrics.damaged);
        while (recent.size() > kTactileHistoryWindow)
        {
            recent.erase(recent.begin());
        }
        historyDirty = true;

        bool warningActive = false;
        if (recent.size() >= kTactileHistoryWindow)
        {
            warningActive = std::all_of(
                recent.begin(),
                recent.end(),
                [](const json &entry) { return entry.is_boolean() && entry.get<bool>(); });
        }

        if (hasPersistentBaseline && !persistentNeedsUpdate && !initializedPersistentFromEpisode)
        {
            const TactileFrameMetrics persistentMetrics =
                computeTactileFrameMetrics(persistentBaseline, *currentFrame);
            json &persistentRecent = cameraHistory["persistent_recent"];
            persistentRecent.push_back(persistentMetrics.damaged);
            while (persistentRecent.size() > kTactileHistoryWindow)
            {
                persistentRecent.erase(persistentRecent.begin());
            }

            bool persistentWindowActive = false;
            bool persistentWindowClean = false;
            if (persistentRecent.size() >= kTactileHistoryWindow)
            {
                persistentWindowActive = std::all_of(
                    persistentRecent.begin(),
                    persistentRecent.end(),
                    [](const json &entry) { return entry.is_boolean() && entry.get<bool>(); });
                persistentWindowClean = std::all_of(
                    persistentRecent.begin(),
                    persistentRecent.end(),
                    [](const json &entry) { return entry.is_boolean() && !entry.get<bool>(); });
            }

            if (persistentWindowActive)
            {
                persistentDetail = "persistent_record " + formatTactileMetrics(persistentMetrics);
                if (!persistentWarningActiveBefore)
                {
                    persistentWarningTriggered = true;
                }
                persistentWarningActiveAfter = true;
                cameraHistory["persistent_baseline_refresh_on_next_boot"] = !bootId.empty();
                cameraHistory["persistent_baseline_refresh_request_boot_id"] = bootId;
                cameraHistory["persistent_baseline_refresh_request_at_ms"] = nowMs;
            }
            else if (persistentWindowClean)
            {
                persistentWarningActiveAfter = false;
                persistentDetail.clear();
            }

            cameraHistory["persistent_fault_active"] = persistentWarningActiveAfter;
            cameraHistory["persistent_fault_detail"] = persistentDetail;
            historyDirty = true;
        }

        TactileValidationFinding finding;
        finding.cameraName = target.cameraName;
        finding.serialNumber = serial;
        finding.side = target.side;
        finding.sensorSlot = target.sensorSlot;
        finding.damaged = metrics.damaged;
        finding.warningActive = warningActive || persistentWarningActiveAfter;
        finding.warningTriggered =
            (warningActive && !warningActiveBefore) || persistentWarningTriggered;
        finding.audioCommand = std::string(target.cameraName) + "_damaged";
        finding.detail = formatTactileMetrics(metrics);
        if (!persistentDetail.empty())
        {
            finding.detail += " | " + persistentDetail;
        }
        if (tactileFindings != nullptr)
        {
            tactileFindings->push_back(std::move(finding));
        }
    }

    if (historyDirty)
    {
        std::string writeError;
        if (!writeTextFileAtomically(tactileHistoryPath(tactileStateDir_),
                                     tactileHistory.dump(2) + "\n",
                                     &writeError))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to persist tactile history: " << writeError << std::endl).str());
        }
    }
    if (tactileFindings != nullptr)
    {
        std::string metadataError;
        if (!updateTactileDamageMetadata(episodeDir, *tactileFindings, &metadataError))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "failed to update tactile damage metadata: episode=" << episodeDir
                << " error=" << metadataError << std::endl).str());
        }
    }
    logPerf((::DA::utils::LogString() << "[PERF] tactile validation done: episode_dir=" << episodeDir
             << " elapsed_ms=" << (steadyNowMs() - tactileValidationStartMs)).str());
}

bool DA::Ugripper::EpisodeManager::updateTactileDamageMetadata(
    const std::string &episodeDir,
    const std::vector<TactileValidationFinding> &tactileFindings,
    std::string *errorMessage) const
{
    const fs::path metadataPath = fs::path(episodeDir) / "metadata.json";
    ordered_json metadata;
    try
    {
        std::ifstream input(metadataPath);
        if (!input.is_open())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "cannot open metadata file: " + metadataPath.string();
            }
            return false;
        }
        input >> metadata;
    }
    catch (const std::exception &ex)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to parse metadata file: " + std::string(ex.what());
        }
        return false;
    }

    if (!metadata.is_object() ||
        !metadata.contains("tactile_damage_sn_list") ||
        !metadata["tactile_damage_sn_list"].is_array())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metadata tactile_damage_sn_list field is missing or invalid";
        }
        return false;
    }

    ordered_json damagedSerialNumbers = ordered_json::array();
    std::set<std::string> seenSerialNumbers;
    for (const auto &finding : tactileFindings)
    {
        if (finding.damaged &&
            !finding.serialNumber.empty() &&
            seenSerialNumbers.insert(finding.serialNumber).second)
        {
            damagedSerialNumbers.push_back(finding.serialNumber);
        }
    }
    metadata["tactile_damage_sn_list"] = std::move(damagedSerialNumbers);
    return writeTextFileAtomically(metadataPath, metadata.dump(2) + "\n", errorMessage);
}
