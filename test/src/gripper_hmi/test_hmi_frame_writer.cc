#include "gripper_hmi_frame_writer.h"

#include <gtest/gtest.h>

#include <vector>

namespace {
using gripper_hmi::FrameWriteResult;
using gripper_hmi::FrameWriter;
using namespace std::chrono_literals;

struct Transport
{
    FrameWriter gate;
    FrameWriter::Clock::time_point time{1s};
    std::chrono::microseconds queuedFor{};
    bool failWrite = false;
    bool failQueueQuery = false;
    size_t pendingPolls = 0;
    std::vector<FrameWriter::Clock::time_point> writes;
    std::vector<size_t> frames;

    FrameWriteResult Send(size_t size, uint32_t baudrate = 115200)
    {
        FrameWriter::Clock::time_point drainedAt;
        return gate.Send(size, baudrate,
            [&]() {
                writes.push_back(time);
                frames.push_back(size);
                drainedAt = time + queuedFor;
                return !failWrite;
            },
            [&]() {
                ++pendingPolls;
                return failQueueQuery ? -1 : (time < drainedAt ? 1 : 0);
            },
            [&]() { return time; },
            [&](auto deadline) { time = deadline; });
    }
};

TEST(GripperHmiFrameWriterTest, BackToBackBeepRgbAndQueryHaveQuietIntervals)
{
    Transport port;
    ASSERT_EQ(port.Send(7), FrameWriteResult::Ok); // beep
    ASSERT_EQ(port.Send(8), FrameWriteResult::Ok); // RGB in the same owner iteration
    ASSERT_EQ(port.Send(7), FrameWriteResult::Ok); // query
    ASSERT_EQ(port.frames, (std::vector<size_t>{7, 8, 7}));
    EXPECT_GE(port.writes[1] - port.writes[0], 25ms + 608us);
    EXPECT_GE(port.writes[2] - port.writes[1], 25ms + 695us);
}

TEST(GripperHmiFrameWriterTest, QuietIntervalStartsAfterQueuedOutputDrains)
{
    Transport port;
    port.queuedFor = 12ms;
    ASSERT_EQ(port.Send(7), FrameWriteResult::Ok);
    port.queuedFor = 0us;
    ASSERT_EQ(port.Send(8), FrameWriteResult::Ok);
    EXPECT_GE(port.writes[1] - port.writes[0], 12ms + 25ms + 608us);
}

TEST(GripperHmiFrameWriterTest, StuckOutputQueueFailsWithinBound)
{
    Transport port;
    port.queuedFor = 10s;
    const auto started = port.time;
    EXPECT_EQ(port.Send(7), FrameWriteResult::QueueTimeout);
    EXPECT_EQ(port.time - started, 50ms);
    EXPECT_EQ(port.writes.size(), 1u);
    EXPECT_LE(port.pendingPolls, 51u);
}

TEST(GripperHmiFrameWriterTest, WriteAndQueueErrorsAreNotTreatedAsSuccess)
{
    Transport port;
    port.failWrite = true;
    EXPECT_EQ(port.Send(7), FrameWriteResult::WriteFailed);
    EXPECT_EQ(port.pendingPolls, 0u);
    port.failWrite = false;
    port.failQueueQuery = true;
    EXPECT_EQ(port.Send(8), FrameWriteResult::QueueQueryFailed);
}

TEST(GripperHmiFrameWriterTest, ReconnectResetsGateAndNewPortHasIndependentTiming)
{
    Transport left, right;
    ASSERT_EQ(left.Send(7), FrameWriteResult::Ok);
    ASSERT_EQ(right.Send(8), FrameWriteResult::Ok);
    EXPECT_EQ(left.writes[0], right.writes[0]);
    left.gate.Reset();
    ASSERT_EQ(left.Send(7), FrameWriteResult::Ok);
    EXPECT_EQ(left.writes[0], left.writes[1]);
}

TEST(GripperHmiFrameWriterTest, LowerBaudrateAccountsForFullFrameWireTime)
{
    Transport port;
    ASSERT_EQ(port.Send(20, 9600), FrameWriteResult::Ok);
    ASSERT_EQ(port.Send(7, 9600), FrameWriteResult::Ok);
    EXPECT_GE(port.writes[1] - port.writes[0], 25ms + 20834us);
}
} // namespace
