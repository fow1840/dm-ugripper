#include "fays_stereo_recorder/imu_clock_mapper.h"
#include "fays_stereo_recorder/imu_callback_queue.h"
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
    int64_t previous=offset;
    for(int i=1000;i<15000;++i) {
        const uint64_t d=1'000'000'000ULL+i*1'000'000ULL, t=d+2'000'000'000ULL;
        m.Observe(d,t,epoch+t+5'000'000);
        const int64_t current=m.EpochOffset();
        EXPECT_LE(std::llabs(current-previous),101000); previous=current;
    }
    EXPECT_NEAR(m.EpochOffset()-offset,5'000'000,10000);
}
