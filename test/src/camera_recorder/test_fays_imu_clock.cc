#include "fays_stereo_recorder/imu_clock_mapper.h"
#include "fays_stereo_recorder/imu_callback_queue.h"
#include "fays_stereo_recorder/camera_timestamp_offsets.h"
#include <gtest/gtest.h>
#include <thread>
using fays_stereo_recorder::ImuClockMapper;
namespace {
constexpr uint64_t epoch = 1'790'000'000'000'000'000ULL;
void Feed(ImuClockMapper& m, int first, int end, int64_t shift = 0, bool spikes = false) {
    for (int i = first; i < end; ++i) {
        uint64_t d = 1'000'000'000ULL + uint64_t(i)*1'000'000;
        uint64_t t = d + 2'000'000'000ULL + shift + ((spikes && i%100>=50 && i%100<70)?uint64_t(70-i%100)*1'000'000:0);
        m.Observe(d, t, epoch + t);
    }
}
}
TEST(FaysImuClock, RejectsIsolatedDelayAndRecovers) {
    ImuClockMapper m; Feed(m,0,1000); const auto before=m.EpochOffset();
    Feed(m,1000,3000,0,true);
    EXPECT_LT(std::llabs(m.EpochOffset()-before),1000);
    Feed(m,3000,4000); EXPECT_TRUE(m.Ready(6'999'000'000ULL));
}
TEST(FaysImuClock, PersistentOffsetIsConfirmedAndSlewedThenTracksAgain) {
    ImuClockMapper m; Feed(m,0,1000); const auto before=m.SteadyOffset();
    m.SetRecording(true);
    Feed(m,1000,1300,10'000'000); EXPECT_EQ(m.SteadyOffset(),before);
    Feed(m,1300,3000,10'000'000);
    EXPECT_GT(m.SteadyOffset(),before); EXPECT_LT(m.SteadyOffset()-before,2'100'000);
    Feed(m,3000,40000,10'000'000); EXPECT_NEAR(m.SteadyOffset()-before,10'000'000,10000);
    Feed(m,40010,80000,0); EXPECT_NEAR(m.SteadyOffset(),before,10000);
}
TEST(FaysImuClock, ReacquiresAfterOutageAndDeviceReset) {
    ImuClockMapper m; Feed(m,0,1000); EXPECT_FALSE(m.Ready(6'000'000'000ULL));
    Feed(m,4000,4400); EXPECT_TRUE(m.Ready(7'399'000'000ULL));
    m.Observe(1000,7'400'000'000ULL,epoch+7'400'000'000ULL); EXPECT_FALSE(m.Ready(7'400'000'000ULL));
    for(int i=1;i<400;++i)m.Observe(1000+i*1'000'000ULL,7'400'000'000ULL+i*1'000'000ULL,epoch+7'400'000'000ULL+i*1'000'000ULL);
    EXPECT_TRUE(m.Ready(7'799'000'000ULL)); EXPECT_GE(m.Resets(),2u);
}
TEST(FaysImuClock, FirstDelayedSampleDoesNotPoisonWarmup) {
    ImuClockMapper m; m.Observe(1'000'000'000,3'020'000'000,epoch+3'020'000'000);
    Feed(m,21,400); EXPECT_TRUE(m.Ready(3'399'000'000)); EXPECT_NEAR(m.SteadyOffset(),2'000'000'000,1);
}
TEST(FaysImuQueue, FullIsExplicitAndPreservesOrderedSamples) {
    fays_stereo_recorder::ImuCallbackQueue<int,4> q;
    for(int i=0;i<4;++i)ASSERT_TRUE(q.Push(i)); EXPECT_FALSE(q.Push(99));
    for(int i=0;i<4;++i){int x;ASSERT_TRUE(q.Pop(x));EXPECT_EQ(x,i);}
    EXPECT_EQ(q.Size(),0u);EXPECT_TRUE(q.Push(8));int x;ASSERT_TRUE(q.Pop(x));EXPECT_EQ(x,8);
}
TEST(FaysImuQueue, ConcurrentProducerConsumerRetainEverySample) {
    fays_stereo_recorder::ImuCallbackQueue<int,128> q;
    std::thread producer([&]{for(int i=0;i<100000;++i)while(!q.Push(i))std::this_thread::yield();});
    for(int i=0;i<100000;++i){int x;while(!q.Pop(x)){} EXPECT_EQ(x,i);}
    producer.join();EXPECT_EQ(q.Size(),0u);
}

TEST(FaysImuClock, LateOutageRecoveryRetainsTrustedOffsetAndRecoversRepeatedly) {
    ImuClockMapper m; Feed(m,0,1000); const auto offset=m.SteadyOffset();
    m.SetRecording(true);
    for (int first : {4000,8000,12000}) {
        EXPECT_FALSE(m.Ready(3'000'000'000ULL + uint64_t(first)*1'000'000));
        Feed(m,first,first+400,20'000'000);
        EXPECT_EQ(m.SteadyOffset(),offset); // First resumed burst cannot rebase.
        Feed(m,first+420,first+1000);
        EXPECT_TRUE(m.Ready(3'000'000'000ULL + uint64_t(first+999)*1'000'000));
        EXPECT_NEAR(m.SteadyOffset(),offset,1000);
    }
}
TEST(FaysImuClock, WallClockStepsAreAlsoLimitedAndRecover) {
    ImuClockMapper m; Feed(m,0,1000); const auto offset=m.EpochOffset();
    m.SetRecording(true);
    int64_t previous=offset;
    for(int i=1000;i<15000;++i) {
        const uint64_t d=1'000'000'000ULL+i*1'000'000ULL, t=d+2'000'000'000ULL;
        m.Observe(d,t,epoch+t+5'000'000);
        const int64_t current=m.EpochOffset();
        EXPECT_LE(std::llabs(current-previous),101000); previous=current;
    }
    EXPECT_NEAR(m.EpochOffset()-offset,5'000'000,10000);
}

namespace {
void FeedWall(ImuClockMapper& m, int first, int end, int64_t shift) {
    for (int i = first; i < end; ++i) {
        const uint64_t d = 1'000'000'000ULL + uint64_t(i) * 1'000'000;
        const uint64_t t = d + 2'000'000'000ULL;
        m.Observe(d, t, epoch + t + shift);
    }
}
}

TEST(FaysImuClock, StandbyRebasesConfirmedNtpStepsInBothDirections) {
    for (const int64_t shift : {-13'000'000'000LL, 13'000'000'000LL}) {
        ImuClockMapper m; Feed(m,0,1000); const auto before=m.EpochOffset();
        FeedWall(m,1000,1001,shift);
        EXPECT_FALSE(m.Ready(4'000'000'000ULL));
        FeedWall(m,1001,1400,shift);
        EXPECT_EQ(m.EpochOffset(),before); // Fewer than five stable windows.
        FeedWall(m,1400,1700,shift);
        EXPECT_TRUE(m.Ready(4'699'000'000ULL));
        EXPECT_NEAR(m.EpochOffset()-before,shift,1000);
        EXPECT_FALSE(m.WallClockFault());
    }
}

TEST(FaysImuClock, StandbyRejectsIsolatedWallSpikesAndUnstableWindows) {
    ImuClockMapper m; Feed(m,0,1000); const auto before=m.EpochOffset();
    FeedWall(m,1000,1051,-13'000'000'000LL);
    FeedWall(m,1051,1700,0);
    EXPECT_TRUE(m.Ready(4'699'000'000ULL));
    EXPECT_EQ(m.EpochOffset(),before);
    // Alternate offsets prevent confirmation, even though all samples are late.
    for (int i=1700; i<2700; i+=200)
        FeedWall(m,i,i+200,(i/200)%2 ? 13'000'000'000LL : 14'000'000'000LL);
    EXPECT_EQ(m.EpochOffset(),before);
    EXPECT_FALSE(m.Ready(5'699'000'000ULL));
    FeedWall(m,2700,3400,0);
    EXPECT_TRUE(m.Ready(6'399'000'000ULL));
    EXPECT_EQ(m.EpochOffset(),before);
}

TEST(FaysImuClock, StopAllowsFilteredFastRecoveryOfDeviceOffset) {
    ImuClockMapper m; Feed(m,0,1000); const auto before=m.SteadyOffset();
    m.SetRecording(true);
    Feed(m,1000,2000,20'000'000);
    EXPECT_LT(m.SteadyOffset()-before,2'000'000);
    m.SetRecording(false);
    Feed(m,2000,2800,20'000'000);
    EXPECT_TRUE(m.Ready(5'819'000'000ULL));
    EXPECT_NEAR(m.SteadyOffset()-before,20'000'000,1000);
}

TEST(FaysImuClock, ConfirmedLargeWallStepFailsAnActiveSession) {
    ImuClockMapper m; Feed(m,0,1000); const auto before=m.EpochOffset();
    m.SetRecording(true);
    FeedWall(m,1000,1400,-13'000'000'000LL);
    EXPECT_FALSE(m.WallClockFault());
    FeedWall(m,1400,1700,-13'000'000'000LL);
    EXPECT_TRUE(m.WallClockFault());
    EXPECT_FALSE(m.Ready(4'699'000'000ULL));
    EXPECT_LT(std::llabs(m.EpochOffset()-before),1'000'000);
}

TEST(FaysImuClock, IsolatedLargeWallSpikeDoesNotFailRecording) {
    ImuClockMapper m; Feed(m,0,1000); const auto before=m.EpochOffset();
    m.SetRecording(true);
    FeedWall(m,1000,1051,13'000'000'000LL);
    EXPECT_TRUE(m.Ready(4'050'000'000ULL)); // Do not drop frames for an unconfirmed spike.
    FeedWall(m,1051,1700,0);
    EXPECT_FALSE(m.WallClockFault());
    EXPECT_TRUE(m.Ready(4'699'000'000ULL));
    EXPECT_EQ(m.EpochOffset(),before);
}

TEST(FaysCameraClock, NewSessionAcceptsBackwardRebaseWithoutStandbyFrames) {
    fays_stereo_recorder::camera_time::SessionTimestampFence fence;
    EXPECT_EQ(fence.Apply(epoch+13'000'000'000ULL,1),epoch+13'000'000'000ULL);
    EXPECT_EQ(fence.Apply(epoch,2),epoch);
    EXPECT_EQ(fence.Apply(epoch+40'000'000ULL,2),epoch+40'000'000ULL);
    EXPECT_EQ(fence.Apply(epoch+20'000'000ULL,2),epoch+40'000'001ULL);
    EXPECT_EQ(fence.Apply(epoch+160'000'000ULL,2),epoch+160'000'000ULL);
}

TEST(FaysCameraClock, StandbyRebasesAndCannotFenceTheNextSession) {
    fays_stereo_recorder::camera_time::SessionTimestampFence fence;
    fence.Apply(epoch+13'000'000'000ULL,1);
    EXPECT_EQ(fence.Apply(epoch+10'000'000'000ULL,0),epoch+10'000'000'000ULL);
    EXPECT_EQ(fence.Apply(epoch,0),epoch);
    EXPECT_EQ(fence.Apply(epoch+1'000'000ULL,2),epoch+1'000'000ULL);
}
