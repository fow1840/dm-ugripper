#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace DA::Ugripper {

// The same snapshot contract as ego_recording_worker.continuous_time_sync_status.
inline bool EgoSyncReady(const nlohmann::json& state, const nlohmann::json& binding,
                         const std::string& serial, const std::string& bootId, double now)
{
    try
    {
        const double age = now - state.at("updated_monotonic_s").get<double>();
        const double sampleAge = now - state.at("result_monotonic_s").get<double>();
        return !serial.empty() && !bootId.empty() && std::isfinite(age) && std::isfinite(sampleAge) &&
               age >= 0 && age <= 5 && sampleAge >= 0 && sampleAge <= 15 &&
               state.at("status") == "ok" && state.at("boot_id") == bootId &&
               state.at("serial") == serial && binding.at("serial") == serial &&
               state.at("bound_at_ms") == binding.at("bound_at_ms") &&
               state.at("package") == "com.ssnwt.helloxr";
    }
    catch (const nlohmann::json::exception&) { return false; }
}

// Presentation only: never changes recording readiness or synchronization policy.
struct EgoSyncFeedbackGate
{
    static constexpr uint64_t kGraceMs = 500;
    bool active = false;
    uint64_t since = 0;
    bool Update(bool pending, bool idle, uint64_t now)
    {
        if (!pending || !idle) { active = false; return false; }
        if (!active) { active = true; since = now; }
        return now >= since && now - since >= kGraceMs;
    }
    uint64_t ToneElapsed(uint64_t now) const
    {
        return active && now >= since && now - since >= kGraceMs ? now - since - kGraceMs : 0;
    }
};

// Initial rising sync cue, followed by three short beeps every 1.5 seconds.
// A zero frequency is silence. Driven by the supervisor, never sleeps.
inline uint16_t EgoSyncTone(uint64_t elapsedMs)
{
    if (elapsedMs < 570)
    {
        constexpr uint16_t rising[] = {1000, 2000, 4000};
        return elapsedMs % 190 < 120 ? rising[elapsedMs / 190] : 0;
    }
    const auto phase = (elapsedMs - 570) % 1500;
    return phase < 570 && phase % 190 < 120 ? 2000 : 0;
}

} // namespace DA::Ugripper
