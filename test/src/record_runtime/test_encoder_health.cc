#include "common/encoder_health.h"
#include "record_runtime/error_code_mapping.h"
#include <gtest/gtest.h>

using namespace ugripper::sensor;

TEST(EncoderHealth, StartupPipeRequiresExplicitReadyAndRejectsEofOrSilence) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    EXPECT_FALSE(WaitEncoderReady(fds[0], 10));
    ASSERT_EQ(::write(fds[1], "R", 1), 1);
    EXPECT_TRUE(WaitEncoderReady(fds[0], 100));
    ASSERT_EQ(::write(fds[1], "X", 1), 1);
    EXPECT_FALSE(WaitEncoderReady(fds[0], 100));
    ::close(fds[1]);
    EXPECT_FALSE(WaitEncoderReady(fds[0], 100));
    ::close(fds[0]);
}

TEST(EncoderHealth, BothSidesMustProduceSamplesBeforeReady) {
    EncoderWatchdog left(100), right(100);
    right.Observe(200);
    EXPECT_FALSE(left.Ready() && right.Ready());
    EXPECT_FALSE(left.Expired(3099));
    EXPECT_TRUE(left.Expired(3100));
    left.Observe(3000);
    EXPECT_TRUE(left.Ready() && right.Ready());
}

TEST(EncoderHealth, SilentSerialPortExpiresEvenAfterSuccessfulSamples) {
    EncoderWatchdog watchdog(0);
    watchdog.Observe(1000);
    EXPECT_FALSE(watchdog.Expired(3999));
    EXPECT_TRUE(watchdog.Expired(4000));
    watchdog.Observe(4001);
    EXPECT_FALSE(watchdog.Expired(4002));
}

TEST(EncoderHealth, RejectsObservedNineMinuteTailLossAndEmptyStreams) {
    const uint64_t last = 1790177805194543786ULL;
    const uint64_t stop = 1790178351642631358ULL;
    EXPECT_FALSE(EncoderCoversTail(last, stop));
    EXPECT_FALSE(EncoderCoversTail(0, stop));
    EXPECT_FALSE(EncoderCoversTail(last, 0));
    EXPECT_TRUE(EncoderCoversTail(stop - 1000000000ULL, stop));
    EXPECT_FALSE(EncoderCoversTail(stop - 1000000001ULL, stop));
    EXPECT_TRUE(EncoderCoversTail(stop + 1, stop));
}

TEST(EncoderHealth, FaultExitCodesKeepSpecificErrorCodes) {
    using DA::Ugripper::UgrCodeForErrorType;
    EXPECT_EQ(UgrCodeForErrorType(EncoderExitErrorType(kEncoderInitExit)), "UGR-30-02");
    EXPECT_EQ(UgrCodeForErrorType(EncoderExitErrorType(kEncoderStaleLeftExit)), "UGR-30-07");
    EXPECT_EQ(UgrCodeForErrorType(EncoderExitErrorType(kEncoderStaleRightExit)), "UGR-30-07");
    EXPECT_EQ(UgrCodeForErrorType("encoder_tail_missing"), "UGR-30-08");
    EXPECT_TRUE(EncoderExitErrorType(0).empty());
    EXPECT_TRUE(EncoderExitErrorType(137).empty());
}
