#pragma once
#include <cerrno>
#include <cstdlib>
#include <sched.h>
#include <sys/resource.h>

namespace DA::utils {
struct AcquisitionPriorityResult {
    enum Status { Disabled, Invalid, Applied, Preserved, Failed } status = Disabled;
    int requested = 0;
    int actual = 0;
    int error = 0;
};
// Called once on the acquisition main thread before spawning workers. Linux
// threads and fork/exec children inherit nice; Runtime never calls this helper.
inline AcquisitionPriorityResult ApplyAcquisitionPriority(const char* setting) {
    AcquisitionPriorityResult r;
    if (!setting || !*setting) return r;
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(setting, &end, 10);
    if (errno || end == setting || *end || value < -10 || value > 0) {
        r.status = AcquisitionPriorityResult::Invalid;
        return r;
    }
    r.requested = static_cast<int>(value);
    errno = 0;
    r.actual = getpriority(PRIO_PROCESS, 0);
    if (errno) { r.status = AcquisitionPriorityResult::Failed; r.error = errno; return r; }
    const int policy = sched_getscheduler(0);
    if (policy < 0) { r.status = AcquisitionPriorityResult::Failed; r.error = errno; return r; }
    // Preserve explicit administrator scheduling policies and higher priority.
    if (policy != SCHED_OTHER || r.actual <= r.requested) {
        r.status = AcquisitionPriorityResult::Preserved;
        return r;
    }
    if (setpriority(PRIO_PROCESS, 0, r.requested) != 0) {
        r.status = AcquisitionPriorityResult::Failed; r.error = errno; return r;
    }
    errno = 0;
    r.actual = getpriority(PRIO_PROCESS, 0);
    r.error = errno;
    r.status = r.error ? AcquisitionPriorityResult::Failed : AcquisitionPriorityResult::Applied;
    return r;
}
}
