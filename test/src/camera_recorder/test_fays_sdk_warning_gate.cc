#include "fays_stereo_recorder/sdk_warning_gate.h"

#include <gtest/gtest.h>

namespace {

using fays_stereo_recorder::SdkWarningGate;
using namespace std::chrono_literals;

constexpr int kNoNewFrame = -2;
constexpr int kTimeout = -6;
constexpr int kDeviceLost = -7;
const auto kStart = SdkWarningGate::Clock::time_point{};

TEST(FaysSdkWarningGate, Normal25FpsPollingNeverWarns) {
    SdkWarningGate gate(kNoNewFrame, kStart);
    for (int frame = 0; frame < 1000; ++frame) {
        const auto frameAt = kStart + frame * 40ms;
        gate.RecordFrame(frameAt);
        for (auto elapsed = 0ms; elapsed < 40ms; elapsed += 1ms) {
            EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, frameAt + elapsed));
        }
    }
}

TEST(FaysSdkWarningGate, StartupWithoutAnyFrameUsesElapsedTime) {
    SdkWarningGate gate(kNoNewFrame, kStart);
    EXPECT_FALSE(gate.HasFrame());
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart));
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 199ms));
    EXPECT_TRUE(gate.ShouldWarn(kNoNewFrame, kStart + 200ms));
    EXPECT_EQ(gate.NoFrameMs(kStart + 200ms), 200);
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 2199ms));
    EXPECT_TRUE(gate.ShouldWarn(kNoNewFrame, kStart + 2200ms));
}

TEST(FaysSdkWarningGate, September11LogShortFrameGapsDoNotWarn) {
    for (const auto ageMs : {0, 3, 26, 33, 34, 39, 40, 72, 111, 114, 116, 131, 164, 180}) {
        SdkWarningGate gate(kNoNewFrame, kStart);
        gate.RecordFrame(kStart);
        EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + std::chrono::milliseconds(ageMs)))
            << "frame_age_ms=" << ageMs;
    }
}

TEST(FaysSdkWarningGate, ReceivedFrameResetsNoFrameTimer) {
    SdkWarningGate gate(kNoNewFrame, kStart);
    gate.RecordFrame(kStart + 150ms);
    EXPECT_TRUE(gate.HasFrame());
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 200ms));
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 349ms));
    EXPECT_TRUE(gate.ShouldWarn(kNoNewFrame, kStart + 350ms));
    EXPECT_EQ(gate.NoFrameMs(kStart + 350ms), 200);
}

TEST(FaysSdkWarningGate, RecoveryAllowsNewStallWarning) {
    SdkWarningGate gate(kNoNewFrame, kStart);
    EXPECT_TRUE(gate.ShouldWarn(kNoNewFrame, kStart + 200ms));
    gate.RecordFrame(kStart + 201ms);
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 240ms));
    EXPECT_TRUE(gate.ShouldWarn(kNoNewFrame, kStart + 401ms));
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 402ms));
}

TEST(FaysSdkWarningGate, ErrorCodesWarnImmediatelyAndAreIndependentlyThrottled) {
    SdkWarningGate gate(kNoNewFrame, kStart);
    EXPECT_TRUE(gate.ShouldWarn(kNoNewFrame, kStart + 200ms));
    EXPECT_TRUE(gate.ShouldWarn(kTimeout, kStart + 201ms));
    EXPECT_TRUE(gate.ShouldWarn(kDeviceLost, kStart + 202ms));
    EXPECT_TRUE(gate.ShouldWarn(-8, kStart + 203ms));
    EXPECT_TRUE(gate.ShouldWarn(-99, kStart + 204ms));
    EXPECT_FALSE(gate.ShouldWarn(kTimeout, kStart + 205ms));
    EXPECT_FALSE(gate.ShouldWarn(kDeviceLost, kStart + 206ms));
    EXPECT_TRUE(gate.ShouldWarn(kTimeout, kStart + 2201ms));
    EXPECT_FALSE(gate.ShouldWarn(kDeviceLost, kStart + 2201ms));
    EXPECT_TRUE(gate.ShouldWarn(kDeviceLost, kStart + 2202ms));
}

TEST(FaysSdkWarningGate, RecentSuccessDoesNotSuppressErrorsOrResetTheirRateLimit) {
    SdkWarningGate gate(kNoNewFrame, kStart);
    gate.RecordFrame(kStart);
    EXPECT_TRUE(gate.ShouldWarn(kTimeout, kStart + 1ms));
    gate.RecordFrame(kStart + 2ms);
    EXPECT_FALSE(gate.ShouldWarn(kNoNewFrame, kStart + 3ms));
    EXPECT_FALSE(gate.ShouldWarn(kTimeout, kStart + 3ms));
    EXPECT_TRUE(gate.ShouldWarn(kDeviceLost, kStart + 4ms));
}

}
