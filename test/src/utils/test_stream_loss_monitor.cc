#include <gtest/gtest.h>
#include "utils/stream_loss_monitor.h"
using DA::utils::StreamLossMonitor;
TEST(StreamLossMonitor, StrictThresholdsAndOneShot) {
    StreamLossMonitor m;
    for (int i=0;i<490;i++) m.Observe(i*10000000ULL);
    m.Observe(5000000000ULL,10,false);
    auto e=m.Check(true); EXPECT_FALSE(e.warning); EXPECT_FALSE(e.error);
    m.Observe(5010000000ULL,1,false);
    e=m.Check(true); EXPECT_TRUE(e.warning); EXPECT_TRUE(e.error);
    e=m.Check(true); EXPECT_FALSE(e.warning); EXPECT_FALSE(e.error);
    EXPECT_TRUE(m.Failed());
}
TEST(StreamLossMonitor, NoStartupBoundaryGapOrCrossEpisodeState) {
    StreamLossMonitor m;
    m.ObservePeriodic(900000000000ULL,40000000);
    EXPECT_EQ(m.Missing(),0u);
    m.ObservePeriodic(900080000000ULL,40000000);
    EXPECT_EQ(m.Missing(),1u);
    EXPECT_FALSE(m.Check(true).error); // first second admission grace
    m=StreamLossMonitor{};
    EXPECT_EQ(m.Missing(),0u); EXPECT_FALSE(m.Failed());
}
TEST(StreamLossMonitor, PeriodicAndSustainedLowRate) {
    StreamLossMonitor normal, low;
    for(int i=0;i<10000;i++) {
        normal.ObservePeriodic(i*8333333ULL,8333333ULL);
        if(i%10) low.ObservePeriodic(i*8333333ULL,8333333ULL);
    }
    EXPECT_EQ(normal.Missing(),0u); EXPECT_FALSE(normal.Check(true).error);
    EXPECT_TRUE(low.Check(true).error); EXPECT_GT(low.Rate(),.09);
}
TEST(StreamLossMonitor, BatchedHostDeliveryIsNotTenMissingSamplesPerBatch) {
    StreamLossMonitor m;
    for(int batch=0;batch<1000;batch++) {
        for(int i=0;i<10;i++) m.ObserveCountSpan((batch*10+9)*1000000ULL,1000000);
        EXPECT_FALSE(m.Check(false).error);
    }
    EXPECT_EQ(m.Missing(),0u);
}

TEST(StreamLossMonitor, CatchupJitterDoesNotAccumulatePhantomLoss) {
    StreamLossMonitor m;
    for(int i=0;i<2000;i++) {
        const uint64_t ts = i*8333333ULL + (i%2 ? 7000000ULL : 0);
        m.ObserveCountSpan(ts,8333333ULL);
        EXPECT_FALSE(m.Check(true).error);
    }
    EXPECT_EQ(m.Missing(),0u);
}

#include "utils/stereo_frame_loss_state.h"
TEST(StereoFrameLossState, OutsideEpisodeAndConsistentAcrossReadersAndWriter) {
    using DA::utils::StereoFrameLossStatePath;
    const auto path = StereoFrameLossStatePath("/data/episode_1/", "stereo_left.mkv");
    EXPECT_EQ(path.parent_path(), "/dev/shm");
    EXPECT_EQ(path, StereoFrameLossStatePath("/data/episode_1", "stereo_left.mkv"));
    EXPECT_NE(path, StereoFrameLossStatePath("/data/episode_2", "stereo_left.mkv"));
    EXPECT_NE(path, StereoFrameLossStatePath("/data/episode_1", "stereo_right.mkv"));
}
