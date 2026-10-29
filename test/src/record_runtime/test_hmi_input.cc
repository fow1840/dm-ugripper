#include "record_runtime/hmi_input.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
using namespace DA::Ugripper;
namespace {
void Click(HmiInputRecognizer& input, unsigned key, uint64_t start, uint64_t duration = 100)
{
    input.Edge(key, true, start);
    input.Edge(key, false, start + duration);
}
void ExpectAction(HmiInputRecognizer& input, HmiInputAction action)
{
    auto events = input.Take();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].first, action);
}
void WorkerDoubleClick(HmiInputController& input)
{
    for (int i = 0; i < 2; ++i) {
        input.Submit(2, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        input.Submit(2, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
TEST(HmiInput, DelayedConsumerRetains161msPressAndDoubleClick)
{
    for (uint64_t delay : {100, 200, 500}) {
        HmiInputRecognizer input;
        input.Reset(HmiInputMode::Idle, 0, 1000);
        Click(input, 2, 1100, 161);
        Click(input, 2, 1422, 81);
        input.Advance(1503 + delay);
        const auto events = input.Take();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].first, HmiInputAction::RecordUp);
        EXPECT_LT(events[0].second, 1550u);
    }
}
TEST(HmiInput, ShortNoiseAndReleaseBounceDoNotBecomeDoubleClick)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Idle, 0, 1000);
    Click(input, 2, 1100, 100); Click(input, 2, 1300, 27);
    input.Advance(2500);
    EXPECT_TRUE(input.Take().empty());
    input.Edge(2, true, 2600); input.Edge(2, false, 2700);
    input.Edge(2, true, 2720); input.Edge(2, false, 2800);
    input.Advance(3000);
    EXPECT_TRUE(input.Take().empty());
}
TEST(HmiInput, DoubleClickUsesEventTimeAndSameKey)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Idle, 0, 1000);
    Click(input, 2, 1100); Click(input, 3, 1400); input.Advance(1600);
    EXPECT_TRUE(input.Take().empty());
    Click(input, 3, 2501); input.Advance(2700);
    EXPECT_TRUE(input.Take().empty());
    Click(input, 3, 2800); input.Advance(3000);
    ExpectAction(input, HmiInputAction::RecordDown);
}
TEST(HmiInput, BusyDropsClicksAndBoundaryHoldMustRelease)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Busy, 0, 1000);
    Click(input, 2, 1100); Click(input, 2, 1400); input.Advance(2000);
    EXPECT_TRUE(input.Take().empty());
    input.Reset(HmiInputMode::Idle, 4, 3000);
    input.Advance(8000);
    EXPECT_TRUE(input.Take().empty());
    input.Edge(2, false, 8100); input.Advance(8150);
    Click(input, 2, 8200); Click(input, 2, 8500); input.Advance(8700);
    ExpectAction(input, HmiInputAction::RecordUp);
}
TEST(HmiInput, ContextChangeCannotJoinTwoClicks)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Recording, 0, 1000);
    Click(input, 2, 1100); input.Advance(1300);
    input.Reset(HmiInputMode::Busy, 0, 1400);
    input.Reset(HmiInputMode::Idle, 0, 1450);
    Click(input, 2, 1500); input.Advance(1700);
    EXPECT_TRUE(input.Take().empty());
}
TEST(HmiInput, TaskMarkerDoesNotAlsoEmitShortOrAudio)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Recording, 0, 1000);
    input.Edge(2, true, 1100); input.Advance(2000);
    ExpectAction(input, HmiInputAction::TaskMarker);
    input.Edge(2, false, 2100); input.Advance(2300);
    EXPECT_TRUE(input.Take().empty());
    input.Edge(3, true, 2400); input.Advance(3300);
    EXPECT_TRUE(input.Take().empty());
    input.Advance(3500);
    ExpectAction(input, HmiInputAction::PostAudio);
}
TEST(HmiInput, BothHandsBindingConsumesUntilFullRelease)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Idle, 0, 1000);
    input.Edge(0, true, 1100); input.Edge(2, true, 1120); input.Advance(3200);
    ExpectAction(input, HmiInputAction::ToggleBinding);
    input.Edge(0, false, 3300); input.Advance(5000);
    input.Edge(2, false, 5100); input.Advance(5200);
    EXPECT_TRUE(input.Take().empty());
}
TEST(HmiInput, RightShutdownPromptThenShutdownNoShortPress)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Idle, 0, 1000);
    input.Edge(2, true, 1100); input.Edge(3, true, 1120); input.Advance(3200);
    ExpectAction(input, HmiInputAction::ShutdownPrompt);
    input.Advance(5200);
    ExpectAction(input, HmiInputAction::Shutdown);
    input.Edge(2, false, 5300); input.Edge(3, false, 5400); input.Advance(5500);
    EXPECT_TRUE(input.Take().empty());
}
TEST(HmiInput, LeftLongPressAndUnmountStayDistinct)
{
    HmiInputRecognizer input;
    input.Reset(HmiInputMode::Idle, 0, 1000);
    Click(input, 0, 1100, 1200); input.Advance(2400);
    ExpectAction(input, HmiInputAction::MarkFailed);
    input.Edge(0, true, 2500); input.Edge(1, true, 2520); input.Advance(6700);
    ExpectAction(input, HmiInputAction::Unmount);
}
TEST(HmiInput, IndependentWorkerRecognizesWhileRuntimeDoesNotPoll)
{
    HmiInputController input;
    input.SetMode(HmiInputMode::Idle);
    WorkerDoubleClick(input);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto event = input.Pop();
    ASSERT_TRUE(event);
    EXPECT_EQ(event->action, HmiInputAction::RecordUp);
    EXPECT_TRUE(input.IsCurrent(*event));
    input.SetMode(HmiInputMode::Busy);
    EXPECT_FALSE(input.IsCurrent(*event));
    EXPECT_FALSE(input.Pop());
    input.SetMode(HmiInputMode::Idle);
    input.LinkReset(false);
    EXPECT_FALSE(input.IsCurrent(*event));
    input.Stop(); input.Stop();
}
TEST(HmiInput, ClaimAtomicallyInvalidatesQueuedActions)
{
    HmiInputController input;
    input.SetMode(HmiInputMode::Idle);
    WorkerDoubleClick(input);
    auto event = input.Pop();
    ASSERT_TRUE(event);
    EXPECT_TRUE(input.Claim(*event, true));
    EXPECT_FALSE(input.Claim(*event, true));
    input.SetMode(HmiInputMode::Idle);
    EXPECT_FALSE(input.Claim(*event, true));
}
TEST(HmiInput, ConcurrentHandsAndModeChangesDoNotReplayBusyInput)
{
    HmiInputController input;
    std::thread left([&] { for (int i = 0; i < 2000; ++i) input.Submit(0, i % 2 == 0); });
    std::thread right([&] { for (int i = 0; i < 2000; ++i) input.Submit(2, i % 2 == 0); });
    for (int i = 0; i < 100; ++i) input.LinkReset(i % 2 == 0);
    left.join(); right.join();
    input.SetMode(HmiInputMode::Idle);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_FALSE(input.Pop());
}
}

TEST(HmiInput, DisconnectedHeldKeyDoesNotBlockHealthyHandOrReplayAfterReconnect)
{
    HmiInputController input;
    input.SetMode(HmiInputMode::Idle);
    input.Submit(0, true);
    input.LinkReset(true);
    input.Submit(0, true); // old hold after reconnect remains suppressed
    WorkerDoubleClick(input);
    auto event = input.Pop();
    ASSERT_TRUE(event);
    EXPECT_EQ(event->action, HmiInputAction::RecordUp);
    EXPECT_FALSE(input.Pop());
    input.Submit(0, false); // rearm that key without synthesizing a short press
}
