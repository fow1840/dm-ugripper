#include "camera_recorder/camera_timing.h"

#include <gtest/gtest.h>

TEST(CameraTimingTest, UsesFrameElapsedTimeForPts)
{
    const auto result = ugripper::camera::MainCameraPtsFromFrameTimeUs(
        1'785'296'245'838'786,
        1'785'296'245'872'120,
        16'667);

    EXPECT_EQ(result.pts_us, 33'334);
    EXPECT_FALSE(result.clamped);
}

TEST(CameraTimingTest, PreservesRealCaptureGap)
{
    const auto result = ugripper::camera::MainCameraPtsFromFrameTimeUs(
        1'000'000,
        1'100'000,
        33'334);

    EXPECT_EQ(result.pts_us, 100'000);
    EXPECT_FALSE(result.clamped);
}

TEST(CameraTimingTest, MinimallyClampsDuplicateTimestamp)
{
    const auto result = ugripper::camera::MainCameraPtsFromFrameTimeUs(
        1'000'000,
        1'016'667,
        16'667);

    EXPECT_EQ(result.raw_pts_us, 16'667);
    EXPECT_EQ(result.pts_us, 16'668);
    EXPECT_EQ(result.adjustment_us, 1);
    EXPECT_TRUE(result.clamped);
    EXPECT_FALSE(result.timestamp_rollback);
}

TEST(CameraTimingTest, MinimallyClampsTimestampRollback)
{
    const auto result = ugripper::camera::MainCameraPtsFromFrameTimeUs(
        1'000'000,
        1'010'000,
        16'667);

    EXPECT_EQ(result.raw_pts_us, 10'000);
    EXPECT_EQ(result.pts_us, 16'668);
    EXPECT_EQ(result.adjustment_us, 6'668);
    EXPECT_TRUE(result.clamped);
    EXPECT_TRUE(result.timestamp_rollback);
}

#include "camera_recorder/main_camera_cadence.h"
#include "camera_recorder/camera_timestamp_offsets.h"

TEST(CameraCadenceTest, RecognizesMeasuredInsertionAndRestoresShutterSpacing)
{
    EXPECT_TRUE(ugripper::camera::IsMainCamera50HzDuplicate({117993,142057,145659,159568}));
    const auto times = ugripper::camera::MainCamera50HzFrameTimes(117993);
    EXPECT_EQ(times[0],117994);
    EXPECT_EQ(times[1],137993);
    EXPECT_EQ(times[2],157993);
    EXPECT_EQ(camera_recorder::camera_time::AddCameraLatencyUs(1000000,
        camera_recorder::camera_time::CameraTimestampSource::Main),972000);
}

TEST(CameraCadenceTest, PreservesNormalCadenceMissingFramesAndRollback)
{
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,20000,40000,60000}));
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,40000,43600,57600}));
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,24000,40000,60000}));
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,24000,27600,80000}));
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,-24000,27600,41600}));
}

TEST(CameraCadenceTest, MatchesMeasuredStopwatchExample)
{
    const std::array<int64_t,4> raw = {
        1789551335615000LL-8000,1789551335639000LL-8000,
        1789551335642000LL-8000,1789551335656000LL-8000};
    ASSERT_TRUE(ugripper::camera::IsMainCamera50HzDuplicate(raw));
    const auto corrected = ugripper::camera::MainCamera50HzFrameTimes(raw[0]);
    EXPECT_EQ(corrected[0]-28000,1789551335579001LL);
    EXPECT_EQ(corrected[1]-28000,1789551335599000LL);
    EXPECT_EQ(corrected[2]-28000,1789551335619000LL);
}

TEST(CameraCadenceTest, AcceptsJitterWithoutUsingFrameNumber)
{
    // Earlier narrow bounds rejected all of these valid long/short patterns.
    EXPECT_TRUE(ugripper::camera::IsMainCamera50HzDuplicate({0,21000,30000,44000}));
    EXPECT_TRUE(ugripper::camera::IsMainCamera50HzDuplicate({0,33000,33400,44500}));
    EXPECT_TRUE(ugripper::camera::IsMainCamera50HzDuplicate({0,24000,33500,44000}));
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,24000,40000,60000}));
}

TEST(CameraCadenceTest, PreservesEachFramesTenSecondAnchorOffset)
{
    const std::array<int64_t,4> encoder = {100000,124000,128000,142000};
    // New XU mapping (+700 us) first appears on the first real frame.
    const std::array<int64_t,4> unix_time = {1100000,1124000,1128700,1142700};
    const auto mapped = ugripper::camera::MainCamera50HzMappedFrameTimes(encoder,unix_time);
    EXPECT_EQ(mapped[0],1100001);
    EXPECT_EQ(mapped[1],1120700);
    EXPECT_EQ(mapped[2],1140700);
    // A negative correction is also retained, leaving monotonic protection to
    // the existing PTS mapper instead of concealing the calibration change.
    const auto negative = ugripper::camera::MainCamera50HzMappedFrameTimes(
        encoder,{1100000,1123500,1127500,1141500});
    EXPECT_EQ(negative[0],1099501);
    EXPECT_EQ(negative[1],1119500);
    EXPECT_EQ(negative[2],1139500);
}

TEST(CameraCadenceTest, RecognizesScreenshotDelayedPairAndCorrectsOnlyWindow)
{
    const std::array<int64_t,4> raw = {0,26587,38223,42648};
    ASSERT_TRUE(ugripper::camera::IsMainCamera50HzDuplicate(raw));
    const auto mapped = ugripper::camera::MainCamera50HzMappedFrameTimes(
        raw,{1000000,1026587,1038223,1042648});
    EXPECT_EQ(mapped[0],1000001);
    EXPECT_EQ(mapped[1],1020000);
    EXPECT_EQ(mapped[2],1040000);
    EXPECT_FALSE(ugripper::camera::IsMainCamera50HzDuplicate({0,26587,38223,90000}));
}

TEST(CameraCadenceTest, RequiresThreeOriginalStableIntervalsBeforeCandidate)
{
    EXPECT_TRUE(ugripper::camera::HasMainCamera50HzStableLeadIn({0,17000,40000,60000}));
    EXPECT_TRUE(ugripper::camera::HasMainCamera50HzStableLeadIn({100000,119000,139000,161000}));
    EXPECT_FALSE(ugripper::camera::HasMainCamera50HzStableLeadIn({0,16999,36999,56999}));
    EXPECT_FALSE(ugripper::camera::HasMainCamera50HzStableLeadIn({0,20000,43001,63001}));
    EXPECT_FALSE(ugripper::camera::HasMainCamera50HzStableLeadIn({0,20000,40000,56000}));
    EXPECT_FALSE(ugripper::camera::HasMainCamera50HzStableLeadIn({0,20000,20000,40000}));
}

#include "camera_recorder/main_camera_h265_continuity.h"

TEST(CameraCadenceTest, TracksWithoutRepeatingLeadInAndResetsOnLoss)
{
    ugripper::camera::MainCameraCadenceTracker t;
    EXPECT_TRUE(t.ShouldCorrect(4,{0,24000,28000,42000},true,true,false));
    EXPECT_TRUE(t.ShouldCorrect(10,{100000,124000,128000,142000},false,true,false));
    EXPECT_TRUE(t.tracked);
    EXPECT_FALSE(t.ShouldCorrect(16,{200000,224000,228000,242000},true,false,false));
    EXPECT_FALSE(t.ShouldCorrect(22,{300000,324000,328000,342000},false,true,false));
    EXPECT_TRUE(t.ShouldCorrect(28,{400000,424000,428000,442000},true,true,false));
}

TEST(CameraCadenceTest, RephasesOnNearbySignalAndSkipsIntraLatency)
{
    ugripper::camera::MainCameraCadenceTracker t;
    ASSERT_TRUE(t.ShouldCorrect(4,{0,24000,28000,42000},true,true,false));
    EXPECT_TRUE(t.ShouldCorrect(9,{100000,124000,128000,142000},false,true,false));
    EXPECT_FALSE(t.ShouldCorrect(15,{200000,234000,244000,248000},true,true,true));
    EXPECT_TRUE(t.ShouldCorrect(21,{300000,324000,328000,342000},false,true,false));
}

TEST(CameraCadenceTest, H265PocAndTransportContinuityAreBothRequired)
{
    using P=ugripper::camera::MainCameraH265Pictures::Picture;
    ugripper::camera::MainCameraH265Continuity c;
    EXPECT_FALSE(c.Observe(P{0,16,true,true},100));
    EXPECT_TRUE(c.Observe(P{1,16,false,false},101));
    EXPECT_FALSE(c.Observe(P{3,16,false,false},102));
    EXPECT_TRUE(c.Observe(P{4,16,false,false},103));
    EXPECT_FALSE(c.Observe(P{5,16,false,false},105));
    EXPECT_FALSE(c.Observe(std::nullopt,106));
    EXPECT_FALSE(c.Observe(P{7,16,false,false},107));
    EXPECT_TRUE(c.Observe(P{0,16,true,true},108));
    EXPECT_FALSE(c.Observe(P{1,8,false,false},109));
}

TEST(CameraCadenceTest, H265RejectsMissingAndTruncatedHeaders)
{
    ugripper::camera::MainCameraH265Pictures p;
    const uint8_t nal[]={0,0,1,2,1,0x80};
    EXPECT_FALSE(p.Inspect(nal,sizeof(nal)));
    EXPECT_FALSE(p.Inspect(nal,3));
    EXPECT_FALSE(p.Inspect(nullptr,0));
}

TEST(CameraCadenceTest, UsesCompressedEvidenceForRepeatAfterIntra)
{
    ugripper::camera::MainCameraCadenceTracker t;
    EXPECT_TRUE(t.ShouldCorrect(51,{0,9600,13200,27000},false,true,true,false,{500,2500,2400}));
    EXPECT_FALSE(t.ShouldCorrect(101,{0,9600,13200,27000},false,true,true,false,{2400,2500,2400}));
    EXPECT_FALSE(t.ShouldCorrect(151,{0,9600,13200,27000},true,false,true,false,{500,2500,2400}));
}

TEST(CameraCadenceTest, DistinguishesRepeatedIntraFromDelayedRealIntra)
{
    ugripper::camera::MainCameraCadenceTracker t;
    EXPECT_TRUE(t.ShouldCorrect(4,{0,24000,28000,42000},true,true,false));
    EXPECT_FALSE(t.ShouldCorrect(10,{0,34000,43600,47200},true,true,false,true,{40000,500,2500}));
    EXPECT_TRUE(t.ShouldCorrect(11,{0,9600,13200,27000},false,true,true,false,{500,2500,2400}));
    EXPECT_TRUE(t.ShouldCorrect(17,{0,26000,37000,42000},false,true,false,true,{40000,2500,2400}));
}
