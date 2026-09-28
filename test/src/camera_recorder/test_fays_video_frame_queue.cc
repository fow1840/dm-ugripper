#include "fays_stereo_recorder/video_frame_queue.h"
#include <gtest/gtest.h>
#include <future>
#include <set>
#include <thread>

using fays_stereo_recorder::VideoFrame;
using fays_stereo_recorder::VideoFrameQueue;

TEST(FaysVideoFrameQueue, OverflowKeepsOrderAndCaptureMetadata) {
    VideoFrameQueue q(2);
    VideoFrame producer, consumer;
    for (uint64_t i = 1; i <= 3; ++i) {
        producer.bytes.assign(16, static_cast<uint8_t>(i));
        producer.faysTsNs = i * 40;
        producer.previousFaysTsNs = (i - 1) * 40;
        producer.sessionId = i;
        ASSERT_TRUE(q.Push(std::move(producer)));
    }
    EXPECT_EQ(q.GetDropCount(), 1u);
    EXPECT_EQ(q.HighWater(), 2u);
    q.Stop();
    for (uint64_t i = 2; i <= 3; ++i) {
        ASSERT_TRUE(q.Pop(consumer));
        EXPECT_EQ(consumer.bytes, std::vector<uint8_t>(16, i));
        EXPECT_EQ(consumer.faysTsNs, i * 40);
        EXPECT_EQ(consumer.previousFaysTsNs, (i - 1) * 40);
        EXPECT_EQ(consumer.sessionId, i);
    }
    EXPECT_FALSE(q.Pop(consumer));
    EXPECT_FALSE(q.Push(std::move(producer)));
}

TEST(FaysVideoFrameQueue, RecyclesAllocationsAfterWarmup) {
    VideoFrameQueue q(2);
    VideoFrame producer, consumer;
    std::set<const uint8_t*> buffers;
    for (int i = 0; i < 100; ++i) {
        producer.bytes.assign(1024, i);
        buffers.insert(producer.bytes.data());
        ASSERT_TRUE(q.Push(std::move(producer)));
        ASSERT_TRUE(q.Pop(consumer));
        EXPECT_EQ(consumer.bytes.front(), static_cast<uint8_t>(i));
    }
    // Ring slots plus producer/consumer each own at most one allocation.
    EXPECT_LE(buffers.size(), 4u);
    EXPECT_EQ(q.GetDropCount(), 0u);
}

TEST(FaysVideoFrameQueue, StopWakesBlockedConsumer) {
    VideoFrameQueue q;
    auto result = std::async(std::launch::async, [&] { VideoFrame f; return q.Pop(f); });
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
    q.Stop();
    EXPECT_EQ(result.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_FALSE(result.get());
}

TEST(FaysVideoFrameQueue, ConcurrentProducerConsumerPreserveOwnership) {
    VideoFrameQueue q(8);
    constexpr uint64_t count = 20000;
    std::thread producer([&] {
        VideoFrame frame;
        for (uint64_t i = 1; i <= count; ++i) {
            frame.faysTsNs = i;
            frame.bytes.assign(4096, i % 251);
            q.Push(std::move(frame));
        }
        q.Stop();
    });
    VideoFrame frame;
    uint64_t previous = 0, consumed = 0;
    while (q.Pop(frame)) {
        EXPECT_GT(frame.faysTsNs, previous);
        EXPECT_EQ(frame.bytes, std::vector<uint8_t>(4096, frame.faysTsNs % 251));
        previous = frame.faysTsNs;
        ++consumed;
    }
    producer.join();
    EXPECT_EQ(previous, count);
    EXPECT_EQ(consumed + q.GetDropCount(), count);
}

TEST(FaysVideoFrameQueue, RejectsZeroCapacity) {
    EXPECT_THROW(VideoFrameQueue(0), std::invalid_argument);
}

#include "fays_stereo_recorder/frame_loss_counter.h"
TEST(FaysFrameLossCounter, JitterAndStrictThreeFrameThreshold) {
    fays_stereo_recorder::FrameLossCounter count(40'000'000);
    EXPECT_EQ(count.Observe(1'000'000'000), 0u);
    EXPECT_EQ(count.Observe(1'041'000'000), 0u);
    EXPECT_EQ(count.Observe(1'080'000'000), 0u);
    EXPECT_EQ(count.Observe(1'240'000'000), 3u);
    EXPECT_EQ(count.Total(), 3u);
    EXPECT_EQ(count.Observe(1'320'000'000), 1u);
    EXPECT_EQ(count.Total(), 4u);
}
TEST(FaysFrameLossCounter, SessionResetAndNonIncreasingTimestamps) {
    fays_stereo_recorder::FrameLossCounter count(40'000'000);
    count.Observe(1'000'000'000);
    EXPECT_EQ(count.Observe(1'000'000'000), 0u);
    EXPECT_EQ(count.Observe(900'000'000), 0u);
    count = fays_stereo_recorder::FrameLossCounter(40'000'000);
    EXPECT_EQ(count.Observe(5'000'000'000), 0u);
    EXPECT_EQ(count.Total(), 0u);
    EXPECT_EQ(count.Observe(5'200'000'000), 4u);
}

TEST(FaysFrameLossCounter, SeparateGapsDoNotTriggerConsecutiveThreshold) {
    fays_stereo_recorder::FrameLossCounter count(40'000'000);
    count.Observe(1'000'000'000);
    EXPECT_EQ(count.Observe(1'120'000'000), 2u);
    EXPECT_EQ(count.Observe(1'160'000'000), 0u);
    EXPECT_EQ(count.Observe(1'280'000'000), 2u);
    EXPECT_EQ(count.Total(), 4u);
    EXPECT_EQ(count.MaxConsecutive(), 2u);
    EXPECT_EQ(count.Observe(1'440'000'000), 3u);
    EXPECT_EQ(count.MaxConsecutive(), 3u);
    EXPECT_EQ(count.Observe(1'640'000'000), 4u);
    EXPECT_EQ(count.MaxConsecutive(), 4u);
    count.Observe(1'720'000'000);
    EXPECT_EQ(count.MaxConsecutive(), 4u);
}
