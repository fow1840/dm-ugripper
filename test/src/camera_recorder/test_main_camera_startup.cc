#include "camera_recorder/main_camera_startup.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
using Buffer = ugripper::camera::MainCameraStartupBuffer;
using Result = Buffer::Result;
constexpr uint64_t kTset = 1'700'000'000'000'000;
constexpr uint64_t kPset = 5'000'000;
constexpr int64_t kBoundary = kTset - 100'000;
Buffer::Frame Frame(uint64_t p, bool key, bool new_anchor = false, bool continuous = true) {
    Buffer::Frame f;
    f.data = {1, 2, 3};
    f.timestamp = {p, new_anchor ? kTset : kTset - 1'000'000};
    f.valid = true; f.clean_keyframe = key; f.continuous = continuous;
    return f;
}
Result Push(Buffer& b, Buffer::Frame f) { return b.Push(std::move(f), kTset, kPset, kBoundary); }

TEST(MainCameraStartup, BackfillsFromOldKeyframeWithoutWaitingForNewIdr) {
    Buffer b;
    EXPECT_EQ(Push(b, Frame(kPset - 80'000, true, false, false)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset - 60'000, false)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset + 15'000, false, true)), Result::Ready);
    auto frames = b.Take();
    ASSERT_EQ(frames.size(), 3u);
    EXPECT_EQ(frames[0].unix_us, kTset - 80'000);
    EXPECT_EQ(frames[1].unix_us - frames[0].unix_us, 20'000);
    EXPECT_EQ(frames[2].unix_us - frames[1].unix_us, 75'000); // Do not invent constant cadence.
    EXPECT_EQ(frames[0].timestamp.xu_time_us, kTset - 1'000'000); // Preserve raw SEI.
    EXPECT_EQ(frames[0].data, (std::vector<uint8_t>{1, 2, 3}));
    EXPECT_EQ(b.size(), 0u);
}
TEST(MainCameraStartup, RejectsPriorSessionEvenWhenKeyframeAndSequenceAreContinuous) {
    Buffer b;
    EXPECT_EQ(Push(b, Frame(kPset - 200'000, true)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset + 10'000, false, true)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset + 30'000, true, true)), Result::Ready);
    auto frames = b.Take();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames.front().unix_us, kTset + 30'000);
}
TEST(MainCameraStartup, BrokenDecodeChainCannotBeReplayed) {
    for (bool rollback : {false, true}) {
        Buffer b;
        Push(b, Frame(kPset, true));
        EXPECT_EQ(Push(b, Frame(rollback ? kPset - 1 : kPset + 1, false, true, rollback)), Result::Waiting);
        EXPECT_EQ(b.size(), 0u);
        EXPECT_EQ(Push(b, Frame(kPset + 20'000, true, true)), Result::Ready);
        EXPECT_EQ(b.Take().size(), 1u);
    }
}
TEST(MainCameraStartup, MissingSeiInvalidatesBufferedGop) {
    Buffer b;
    Push(b, Frame(kPset, true));
    auto invalid = Frame(kPset + 10'000, false); invalid.valid = false;
    EXPECT_EQ(Push(b, std::move(invalid)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset + 20'000, false, true)), Result::Waiting);
    EXPECT_EQ(b.size(), 0u);
}
TEST(MainCameraStartup, AcknowledgementBeforeKeyframeDoesNotAdmitAnotherOldAnchor) {
    Buffer b;
    EXPECT_EQ(Push(b, Frame(kPset, false, true)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset + 20'000, true)), Result::Waiting);
    EXPECT_EQ(b.size(), 0u);
    EXPECT_EQ(Push(b, Frame(kPset + 40'000, true, true)), Result::Ready);
}
TEST(MainCameraStartup, NewKeyframeCanRecoverAfterDiscontinuity) {
    Buffer b;
    Push(b, Frame(kPset, true));
    EXPECT_EQ(Push(b, Frame(kPset + 100'000, true, true, false)), Result::Ready);
    EXPECT_EQ(b.Take().size(), 1u);
    EXPECT_EQ(b.dropped(), 1u);
}
TEST(MainCameraStartup, FrameAndByteLimitsNeverReleaseUnacknowledgedFrames) {
    Buffer b;
    for (std::size_t i = 0; i < Buffer::kMaxFrames; ++i)
        ASSERT_EQ(Push(b, Frame(kPset + i, i == 0)), Result::Waiting);
    EXPECT_EQ(Push(b, Frame(kPset + Buffer::kMaxFrames, false)), Result::Overflow);
    EXPECT_EQ(b.size(), 0u);
    auto large = Frame(kPset, true); large.data.resize(Buffer::kMaxBytes + 1);
    EXPECT_EQ(Push(b, std::move(large)), Result::Overflow);
    EXPECT_EQ(b.size(), 0u);
}
TEST(MainCameraStartup, RejectsInvalidMappingAndAllowsSignedPreSetDifference) {
    Buffer b;
    EXPECT_EQ(b.Push(Frame(1, true), 10, 100, 1), Result::Waiting);
    EXPECT_EQ(b.size(), 0u);
    EXPECT_EQ(Push(b, Frame(kPset - 50'000, true, true)), Result::Ready);
    EXPECT_EQ(b.Take().front().unix_us, kTset - 50'000);
}
} // namespace
