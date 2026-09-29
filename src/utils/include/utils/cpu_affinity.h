#pragma once
#include <sched.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "utils/ugripper_logger.h"

namespace DA::utils {
enum class CpuRole { Auxiliary, Fays, Video, Sensor, EncoderRequest };
struct CpuAffinityPlan {
    cpu_set_t cpus{};
    bool enabled = false;
    // Safe in a forked child: no allocation or logger locks.
    bool Apply() const { return !enabled || sched_setaffinity(0, sizeof(cpus), &cpus) == 0; }
};
inline CpuAffinityPlan MakeCpuAffinityPlan(CpuRole role) {
    CpuAffinityPlan plan;
    const char* profile = std::getenv("UGRIPPER_CPU_PROFILE");
    if (!profile || !*profile || std::strcmp(profile, "off") == 0) return plan;
    if (std::strcmp(profile, "fays_split") != 0) {
        DM_LOG_WARN("[CPU] unknown UGRIPPER_CPU_PROFILE={}, keeping inherited affinity", profile);
        return plan;
    }
    // This profile is for RK3588's four small + four big cores only.
    int small = 0, big = 0;
    std::ifstream("/sys/devices/system/cpu/cpu0/cpu_capacity") >> small;
    std::ifstream("/sys/devices/system/cpu/cpu4/cpu_capacity") >> big;
    if (!small || big <= small) {
        DM_LOG_WARN("[CPU] fays_split unavailable on this CPU topology");
        return plan;
    }
    // Capture the parent allowance before narrowing Runtime; descendants may
    // widen back to that original allowance, never to an invented CPU set.
    unsigned base = 0;
    if (const char* value = std::getenv("UGRIPPER_CPU_BASE_MASK")) {
        char* end = nullptr;
        base = static_cast<unsigned>(std::strtoul(value, &end, 16));
        if (!end || *end) base = 0;
    } else {
        cpu_set_t inherited;
        if (sched_getaffinity(0, sizeof(inherited), &inherited) != 0) return plan;
        for (int cpu = 0; cpu < 8; ++cpu) if (CPU_ISSET(cpu, &inherited)) base |= 1u << cpu;
        if (base == 255) setenv("UGRIPPER_CPU_BASE_MASK", "ff", 1);
    }
    if (base != 255) {
        DM_LOG_WARN("[CPU] fays_split requires all CPUs 0-7 in original allowance; keeping inherited affinity");
        return plan;
    }
    const unsigned mask = role == CpuRole::Auxiliary ? 0x0f : (role == CpuRole::Fays || role == CpuRole::EncoderRequest) ? 0x30 : role == CpuRole::Video ? 0xc0 : 0xff;
    CPU_ZERO(&plan.cpus);
    for (int cpu = 0; cpu < 8; ++cpu) if (mask & (1u << cpu)) CPU_SET(cpu, &plan.cpus);
    plan.enabled = true;
    return plan;
}
inline void ConfigureCpuAffinity(CpuRole role) {
    const auto plan = MakeCpuAffinityPlan(role);
    if (!plan.Apply()) DM_LOG_WARN("[CPU] failed to set affinity: {}", std::strerror(errno));
    else if (plan.enabled) DM_LOG_DEBUG("[CPU] applied fays_split role={}", static_cast<int>(role));
}
}
