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

TEST(CameraCadenceTest, RephasesOnNearbySignalAndTracksConfirmedPostIntra)
{
    ugripper::camera::MainCameraCadenceTracker t;
    ASSERT_TRUE(t.ShouldCorrect(4,{0,24000,28000,42000},true,true,false));
    EXPECT_TRUE(t.ShouldCorrect(9,{100000,124000,128000,142000},false,true,false));
    EXPECT_TRUE(t.ShouldCorrect(15,{200000,234000,244000,248000},true,true,true));
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

#include "camera_recorder/main_camera_h264_continuity.h"
namespace {
struct AvcTestBits {
    std::vector<uint8_t> bytes;
    unsigned pos=0;
    void Put(uint32_t v,unsigned n) {
        for(unsigned i=n;i>0;--i) {if(pos%8==0) bytes.push_back(0);bytes.back()|=((v>>(i-1))&1)<<(7-pos%8);++pos;}
    }
    void Ue(uint32_t v) {unsigned n=0;for(auto x=v+1;x>1;x>>=1)++n;Put(0,n);Put(v+1,n+1);}
    void Nal(std::vector<uint8_t>& out,uint8_t h) {
        Put(1,1);while(pos%8)Put(0,1);out.insert(out.end(),{0,0,1,h});
        unsigned zeros=0;for(auto b:bytes){if(zeros==2&&b<=3){out.push_back(3);zeros=0;}out.push_back(b);zeros=b==0?zeros+1:0;}
    }
};
std::vector<uint8_t> AvcAu(unsigned frame,bool idr=false,unsigned slice=0,bool reference=true,bool headers=false) {
    std::vector<uint8_t> out;
    if(headers) {
        AvcTestBits s;s.Put(66,8);s.Put(0,8);s.Put(30,8);s.Ue(0);s.Ue(0);s.Ue(0);s.Ue(0);s.Ue(1);s.Put(0,1);s.Ue(39);s.Ue(29);s.Put(1,1);s.Nal(out,0x67);
        AvcTestBits p;p.Ue(0);p.Ue(0);p.Put(0,1);p.Put(0,1);p.Ue(0);p.Nal(out,0x68);
    }
    AvcTestBits b;b.Ue(0);b.Ue(slice);b.Ue(0);b.Put(frame,4);b.Nal(out,(reference?0x60:0)|(idr?5:1));return out;
}
}
TEST(CameraCadenceTest, H264ParsesReferencePicturesAndResetsOnUnsupportedSlices) {
    ugripper::camera::MainCameraH264Pictures p;
    auto inspect=[&](const auto& bytes){return p.Inspect(bytes.data(),bytes.size());};
    auto first=inspect(AvcAu(0,true,2,true,true));ASSERT_TRUE(first);EXPECT_TRUE(first->intra);EXPECT_GT(first->vcl_bytes,0u);
    auto second=inspect(AvcAu(1));ASSERT_TRUE(second);EXPECT_EQ(second->frame_num,1u);
    EXPECT_FALSE(inspect(AvcAu(2,false,1))); // B picture invalidates cached headers
    EXPECT_FALSE(inspect(AvcAu(3)));
    ASSERT_TRUE(inspect(AvcAu(0,true,2,true,true)));
    EXPECT_FALSE(inspect(AvcAu(1,false,0,false))); // non-reference frame_num is not continuity evidence
    const std::vector<uint8_t> truncated{0,0,1,0x67};EXPECT_FALSE(inspect(truncated));
}
TEST(CameraCadenceTest, H264ContinuityRequiresBothReferenceAndTransportSequence) {
    using P=ugripper::camera::MainCameraH264Pictures::Picture;
    ugripper::camera::MainCameraH264Continuity c;
    EXPECT_FALSE(c.Observe(P{14,4,false,false,100},20));
    EXPECT_TRUE(c.Observe(P{15,4,false,false,100},21));
    EXPECT_TRUE(c.Observe(P{0,4,false,false,100},22));
    EXPECT_FALSE(c.Observe(P{2,4,false,false,100},23));
    EXPECT_FALSE(c.Observe(P{3,4,false,false,100},25));
    EXPECT_TRUE(c.Observe(P{0,4,true,true,100},26));
    EXPECT_FALSE(c.Observe(std::nullopt,27));
    EXPECT_FALSE(c.Observe(P{1,4,false,false,100},28));
}

TEST(CameraCadenceTest, PostIntraRepeatRequiresLockedPhaseContinuityAndSignature)
{
    using ugripper::camera::MainCameraCadenceTracker;
    const std::array<int64_t,4> measured{0,25056,37510,42156};
    const std::array<std::size_t,3> sizes{1527,1961,1688};
    MainCameraCadenceTracker t;
    EXPECT_FALSE(t.ShouldCorrect(351,measured,false,true,true,false,sizes));
    ASSERT_TRUE(t.ShouldCorrect(345,{0,24000,28000,42000},true,true,false));
    EXPECT_FALSE(t.ShouldCorrect(349,measured,false,true,true,false,sizes));
    EXPECT_TRUE(t.ShouldCorrect(351,measured,false,true,true,false,sizes));
    EXPECT_FALSE(t.ShouldCorrect(357,{0,20000,40000,60000},false,true,true,false,sizes));
    EXPECT_FALSE(t.ShouldCorrect(363,measured,false,false,true,false,sizes));
    EXPECT_FALSE(t.ShouldCorrect(369,measured,false,true,true,false,sizes));
}

TEST(CameraCadenceTest, RephasesAfterIntraWhenShortGapIsSecond)
{
    ugripper::camera::MainCameraCadenceTracker t;
    ASSERT_TRUE(t.ShouldCorrect(5294,{0,24000,28000,42000},true,true,false));
    EXPECT_FALSE(t.ShouldCorrect(5300,{0,19000,44000,56500},false,true,false,true,{55000,1500,2000}));
    // Failed expected I candidate advances prediction to 5306. This is not a
    // nearby phase any more; the next call must retain the prior phase evidence.
    EXPECT_TRUE(t.ShouldCorrect(5301,{0,25056,37510,42156},false,true,true,false,{1527,1961,1688}));
}

#include "camera_recorder/tactile_timing.h"
TEST(TactileCaptureTiming, ExactUnixOriginIgnoresRoundedDisplay)
{
    auto origin = ugripper::camera::TactileUnixOriginFromFfmpeg(
        "demuxer+ffmpeg -> ist_index:0:0 pkt_pts:0 off:-1789621040150679 off_time:-1.78962e+09");
    ASSERT_TRUE(origin);
    EXPECT_EQ(*origin, 1789621040150679LL);
    for (const auto line : {"demuxer -> off:-1789621040150679", "demuxer+ffmpeg -> off:-3132625604", "demuxer+ffmpeg -> off:NOPTS", "demuxer+ffmpeg -> off:-1789621040150679bad", "demuxer+ffmpeg -> off:-9223372036854775808"})
        EXPECT_FALSE(ugripper::camera::TactileUnixOriginFromFfmpeg(line));
}
