#include <gtest/gtest.h>
#include "utils/stream_loss_monitor.h"
#include "utils/stereo_frame_loss_state.h"
using DA::utils::StreamLossMonitor;

TEST(StreamLossMonitor, StartupThreeFrameDeficitDoesNotStopAndCanDilute) {
    StreamLossMonitor m;
    m.ObserveCountSpan(0, 8333333, true);
    m.ObserveCountSpan(36000000, 8333333, true);
    EXPECT_EQ(m.MaxConsecutive(), 3u);
    EXPECT_TRUE(m.FinalRateExceeded());
    EXPECT_FALSE(m.Check(true, 7).error);
    // A real startup deficit remains counted, but later good frames lower its rate.
    for (int i=1; i<=200; ++i) m.ObserveCountSpan(36000000ULL+i*8333333ULL,8333333,true);
    EXPECT_EQ(m.Missing(), 3u);
    EXPECT_FALSE(m.FinalRateExceeded());
}
TEST(StreamLossMonitor, FinalRateStrictlyGreaterThanTwoPercentWithoutGrace) {
    StreamLossMonitor m;
    for (int i=0;i<98;++i) m.Observe(i*1000000ULL);
    m.Observe(99000000,2,false);
    EXPECT_DOUBLE_EQ(m.Rate(),.02);
    EXPECT_FALSE(m.FinalRateExceeded());
    m.Observe(100000000,1,false);
    EXPECT_TRUE(m.FinalRateExceeded());
    EXPECT_FALSE(m.Check(true).error); // never stop on cumulative rate
}
TEST(StreamLossMonitor, MainAndStereoAllowThreeButRejectFourOnce) {
    StreamLossMonitor m;
    m.Observe(0);
    m.Observe(40000000,3);
    EXPECT_FALSE(m.Check(true,3).error);
    m.Observe(90000000,4);
    EXPECT_TRUE(m.Check(true,3).error);
    EXPECT_FALSE(m.Check(true,3).error);
    EXPECT_TRUE(m.Failed());
}
TEST(StreamLossMonitor, TactileRejectsEightEvenWhenOverallRateIsLow) {
    StreamLossMonitor m;
    for(int i=0;i<10000;++i) m.ObservePeriodic(i*8333333ULL,8333333);
    m.ObservePeriodic(10007*8333333ULL,8333333); // seven missing
    EXPECT_FALSE(m.Check(true,7).error);
    m.ObservePeriodic(10016*8333333ULL,8333333); // eight missing
    EXPECT_FALSE(m.FinalRateExceeded());
    EXPECT_TRUE(m.Check(true,7).error);
    EXPECT_FALSE(m.Check(true,7).error);
}
TEST(StreamLossMonitor, InvalidMainFramesAccumulateUntilRetainedFrame) {
    StreamLossMonitor m;
    m.Observe(0);
    m.Observe(10000000,2,false); // one sequence gap plus one invalid frame
    EXPECT_FALSE(m.Check(true,3).error);
    m.Observe(20000000,1,false);
    EXPECT_FALSE(m.Check(true,3).error);
    m.Observe(30000000,1,false);
    EXPECT_TRUE(m.Check(true,3).error);
    m=StreamLossMonitor{};
    m.Observe(0);
    m.Observe(10000000,3);
    m.Observe(20000000,3);
    EXPECT_FALSE(m.Check(true,3).error); // separate gaps never sum into one burst
}
TEST(StreamLossMonitor, FirstSampleAndNextSessionHaveNoBoundaryGap) {
    StreamLossMonitor m;
    m.ObservePeriodic(900000000000ULL,40000000);
    EXPECT_EQ(m.Missing(),0u);
    m.ObservePeriodic(900200000000ULL,40000000);
    EXPECT_TRUE(m.Check(true,3).error);
    m=StreamLossMonitor{};
    EXPECT_EQ(m.Missing(),0u);
    EXPECT_EQ(m.MaxConsecutive(),0u);
    EXPECT_FALSE(m.Failed());
}
TEST(StreamLossMonitor, BatchedSensorDeliveryCatchesUpBeforeFinalCheck) {
    StreamLossMonitor m;
    for(int batch=0;batch<1000;batch++) {
        for(int i=0;i<10;i++) m.ObserveCountSpan((batch*10+9)*1000000ULL,1000000);
        EXPECT_FALSE(m.Check(false).error);
    }
    EXPECT_EQ(m.Missing(),0u);
    EXPECT_FALSE(m.FinalRateExceeded());
}
TEST(StreamLossMonitor, CatchupJitterDoesNotAccumulatePhantomLoss) {
    StreamLossMonitor m;
    for(int i=0;i<2000;i++) {
        const uint64_t ts = i*8333333ULL + (i%2 ? 7000000ULL : 0);
        m.ObserveCountSpan(ts,8333333,true);
        EXPECT_FALSE(m.Check(true,7).error);
    }
    EXPECT_EQ(m.Missing(),0u);
}
TEST(StereoFrameLossState, OutsideEpisodeAndConsistentAcrossReadersAndWriter) {
    using DA::utils::StereoFrameLossStatePath;
    const auto path = StereoFrameLossStatePath("/data/episode_1/", "stereo_left.mkv");
    EXPECT_EQ(path.parent_path(), "/dev/shm");
    EXPECT_EQ(path, StereoFrameLossStatePath("/data/episode_1", "stereo_left.mkv"));
    EXPECT_NE(path, StereoFrameLossStatePath("/data/episode_2", "stereo_left.mkv"));
    EXPECT_NE(path, DA::utils::StreamLossResultPath("/data/episode_1", "stereo_left.mkv"));
}

#include "utils/stream_loss_reporting.h"
#include <nlohmann/json.hpp>
#include <unistd.h>
TEST(StreamLossResult, PendingIsOverwrittenByFinalCountsIncludingDrainedSamples) {
    const auto episode = std::filesystem::path("/tmp/loss-policy-test-") / std::to_string(getpid());
    const std::string stream = "encoder_right";
    const auto path = DA::utils::StreamLossResultPath(episode, stream);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove(path,e); } } cleanup{path};
    StreamLossMonitor m;
    ASSERT_TRUE(DA::utils::WriteStreamLossResult(episode,stream,m,false));
    nlohmann::json result;
    { std::ifstream in(path); in >> result; }
    EXPECT_FALSE(result["finalized"].get<bool>());
    m.ObserveCountSpan(0,1000000);
    m.ObserveCountSpan(10000000,1000000); // temporarily delayed batch
    EXPECT_TRUE(m.FinalRateExceeded());
    for(int i=0;i<9;++i) m.ObserveCountSpan(10000000,1000000); // final drain
    ASSERT_TRUE(DA::utils::WriteStreamLossResult(episode,stream,m,true));
    { std::ifstream in(path); in >> result; }
    EXPECT_TRUE(result["finalized"].get<bool>());
    EXPECT_FALSE(result["rate_exceeded"].get<bool>());
    EXPECT_EQ(result["received"],11);
    EXPECT_EQ(result["missing"],0);
    m.ObserveCountSpan(20000000,1000000);
    ASSERT_TRUE(DA::utils::WriteStreamLossResult(episode,stream,m,true));
    { std::ifstream in(path); in >> result; }
    EXPECT_TRUE(result["rate_exceeded"].get<bool>());
    // Reusing a path cannot reuse its previous failure/success result.
    ASSERT_TRUE(DA::utils::WriteStreamLossResult(episode,stream,{},false));
    { std::ifstream in(path); in >> result; }
    EXPECT_FALSE(result["finalized"].get<bool>());
}
