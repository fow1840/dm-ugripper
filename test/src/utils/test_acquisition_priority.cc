#include <gtest/gtest.h>
#include "utils/acquisition_priority.h"
using DA::utils::AcquisitionPriorityResult;
using DA::utils::ApplyAcquisitionPriority;
TEST(AcquisitionPriority, UnsetDoesNotAlterScheduling) {
    errno = 0;
    const auto before = getpriority(PRIO_PROCESS, 0);
    const auto policy = sched_getscheduler(0);
    EXPECT_EQ(ApplyAcquisitionPriority(nullptr).status, AcquisitionPriorityResult::Disabled);
    EXPECT_EQ(ApplyAcquisitionPriority("").status, AcquisitionPriorityResult::Disabled);
    EXPECT_EQ(getpriority(PRIO_PROCESS, 0), before);
    EXPECT_EQ(sched_getscheduler(0), policy);
}
TEST(AcquisitionPriority, RejectsInvalidOrExcessivePriority) {
    const auto before = getpriority(PRIO_PROCESS, 0);
    for (const char* setting : {"-20", "-11", "1", "bad", "-10x", "99999999999999999999"})
        EXPECT_EQ(ApplyAcquisitionPriority(setting).status, AcquisitionPriorityResult::Invalid);
    EXPECT_EQ(getpriority(PRIO_PROCESS, 0), before);
}
TEST(AcquisitionPriority, PreservesExistingPriorityWhenAlreadySufficient) {
    const auto before = getpriority(PRIO_PROCESS, 0);
    if (before > 0) GTEST_SKIP() << "test runner has reduced priority";
    EXPECT_EQ(ApplyAcquisitionPriority("0").status, AcquisitionPriorityResult::Preserved);
    EXPECT_EQ(getpriority(PRIO_PROCESS, 0), before);
}
