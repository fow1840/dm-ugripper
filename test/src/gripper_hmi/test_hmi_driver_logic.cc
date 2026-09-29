#include "gripper_hmi_driver_logic.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

using gripper_hmi::driver_logic::CalibrationWriteChunkStatusAction;

TEST(GripperHmiDriverLogicTest, RetriesBothBeepEnableAndSilenceCommands)
{
    const GripperBeepState enabled{50, 4000};
    const GripperBeepState silenced{0, 0};

    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(enabled, 5, 100, 119, 20));
    EXPECT_TRUE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(enabled, 5, 100, 120, 20));
    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(silenced, 5, 100, 119, 20));
    EXPECT_TRUE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(silenced, 5, 100, 120, 20));

    EXPECT_TRUE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(enabled, 0, 100, 120, 20));
    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(silenced, 0, 100, 120, 20));
    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(silenced, 0, 100, 1099, 20));
    EXPECT_TRUE(gripper_hmi::driver_logic::ShouldWriteBeepCommand(silenced, 0, 100, 1100, 20));
    // Some firmware retains the last frequency while duty=0 (already silent).
    EXPECT_TRUE(gripper_hmi::driver_logic::ShouldWriteBeepCommand({0, 1000}, 0, 100, 1100, 20));
}

TEST(GripperHmiDriverLogicTest, PendingQueryWaitsForQuietWindow)
{
    using gripper_hmi::driver_logic::StateQueryWaitMs;
    EXPECT_EQ(StateQueryWaitMs(100, true, 50, 100), 25u);
    EXPECT_EQ(StateQueryWaitMs(124, true, 50, 100), 1u);
    EXPECT_EQ(StateQueryWaitMs(125, true, 50, 100), 1u);
    EXPECT_EQ(StateQueryWaitMs(100, false, 50, 100), 950u);
    EXPECT_EQ(StateQueryWaitMs(100, true, 0, 0), 1u);
}

TEST(GripperHmiDriverLogicTest, PostQueryRetryUsesQuietDeadlineEvenWithoutLedEffect)
{
    using gripper_hmi::driver_logic::OwnerWaitMs;
    // A query just completed at 100ms; the retry is due but needs 25ms quiet.
    EXPECT_EQ(OwnerWaitMs(100, false, 100, 75, 20, 100, 100, false, 0), 20u);
    EXPECT_EQ(OwnerWaitMs(120, false, 100, 75, 20, 100, 100, false, 0), 5u);
    EXPECT_EQ(OwnerWaitMs(124, false, 100, 75, 20, 100, 100, false, 0), 1u);
    // While a pending query awaits quiet, an overdue beep/LED cannot spin.
    EXPECT_EQ(OwnerWaitMs(100, true, 50, 100, 20, 0, 100, true, 0), 20u);
}

TEST(GripperHmiDriverLogicTest, LedKeepaliveAndBudgetRemainScheduledWithoutEffects)
{
    using gripper_hmi::driver_logic::OwnerWaitMs;
    // OFF beep and query have a 1s period; same-color refresh is still 250ms.
    EXPECT_EQ(OwnerWaitMs(100, false, 100, 100, 1000, 100, 100, false, 0), 20u);
    // An already-due LED yields only until the current write budget expires.
    EXPECT_EQ(OwnerWaitMs(351, false, 100, 100, 1000, 100, 350, false, 0), 4u);
}

TEST(GripperHmiDriverLogicTest, KeepaliveBudgetDefersWithoutDroppingReadyCommands)
{
    using gripper_hmi::driver_logic::KeepaliveCommand;
    using gripper_hmi::driver_logic::SelectKeepaliveCommand;
    EXPECT_EQ(SelectKeepaliveCommand(true, true, 90, 80, 100, 104), KeepaliveCommand::None);
    EXPECT_EQ(SelectKeepaliveCommand(true, true, 90, 80, 100, 105), KeepaliveCommand::Led);
    // Once LED was served the still-pending beep wins at the next budget slot.
    EXPECT_EQ(SelectKeepaliveCommand(true, false, 90, 355, 105, 110), KeepaliveCommand::Beep);
    EXPECT_EQ(SelectKeepaliveCommand(false, false, 90, 80, 0, 100), KeepaliveCommand::None);
}

TEST(GripperHmiDriverLogicTest, KeepaliveArbitrationServesEarliestDeadline)
{
    using gripper_hmi::driver_logic::KeepaliveCommand;
    using gripper_hmi::driver_logic::SelectKeepaliveCommand;
    EXPECT_EQ(SelectKeepaliveCommand(true, true, 70, 80, 0, 100), KeepaliveCommand::Beep);
    EXPECT_EQ(SelectKeepaliveCommand(true, true, 80, 80, 0, 100), KeepaliveCommand::Led);
    // A beep excluded by the query quiet window cannot consume the budget.
    EXPECT_EQ(SelectKeepaliveCommand(false, true, 70, 80, 0, 100), KeepaliveCommand::Led);
}

TEST(GripperHmiDriverLogicTest, ClassifiesRetryableAndAbortRecoveryStatuses)
{
    EXPECT_TRUE(gripper_hmi::driver_logic::IsRetryableCalibrationStatus(GripperHmiProtocol::kStatusMissingData));
    EXPECT_TRUE(
        gripper_hmi::driver_logic::IsRetryableCalibrationStatus(GripperHmiProtocol::kStatusZeroDataChecksumError));
    EXPECT_TRUE(gripper_hmi::driver_logic::IsRetryableCalibrationStatus(GripperHmiProtocol::kStatusChecksumError));
    EXPECT_TRUE(gripper_hmi::driver_logic::IsRetryableCalibrationStatus(GripperHmiProtocol::kStatusAddressOutOfLimit));
    EXPECT_FALSE(gripper_hmi::driver_logic::IsRetryableCalibrationStatus(GripperHmiProtocol::kStatusInvalidCommand));

    EXPECT_TRUE(
        gripper_hmi::driver_logic::IsCalibrationAbortRecoveryStatus(GripperHmiProtocol::kStatusMissingData));
    EXPECT_TRUE(
        gripper_hmi::driver_logic::IsCalibrationAbortRecoveryStatus(GripperHmiProtocol::kStatusZeroDataChecksumError));
    EXPECT_FALSE(
        gripper_hmi::driver_logic::IsCalibrationAbortRecoveryStatus(GripperHmiProtocol::kStatusChecksumError));
}

TEST(GripperHmiDriverLogicTest, AcceptsCalibrationAckTokenForCurrentAndNextPacket)
{
    EXPECT_TRUE(gripper_hmi::driver_logic::IsCalibrationWriteAckToken(0, 0));
    EXPECT_TRUE(gripper_hmi::driver_logic::IsCalibrationWriteAckToken(0, 1));
    EXPECT_TRUE(gripper_hmi::driver_logic::IsCalibrationWriteAckToken(7, 7));
    EXPECT_TRUE(gripper_hmi::driver_logic::IsCalibrationWriteAckToken(7, 8));
    EXPECT_FALSE(gripper_hmi::driver_logic::IsCalibrationWriteAckToken(7, 6));
    EXPECT_FALSE(gripper_hmi::driver_logic::IsCalibrationWriteAckToken(7, 9));
}

TEST(GripperHmiDriverLogicTest, ParsesCalibrationHeaderAndComputesRequiredChunkCount)
{
    std::array<uint8_t, gripper_hmi::kCalibrationPayloadSize> raw = {};
    gripper_hmi::GripperCalibrationHeader header{};
    header.payloadSize = 432;
    std::memcpy(raw.data(), &header, sizeof(header));

    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received = {};
    received[0] = true;
    received[1] = true;

    gripper_hmi::GripperCalibrationHeader parsed{};
    size_t required_chunk_count = 0;
    ASSERT_TRUE(gripper_hmi::driver_logic::TryParseCalibrationHeader(raw, received, &parsed, &required_chunk_count));
    EXPECT_EQ(std::memcmp(parsed.magic, "UCAL", 4), 0);
    EXPECT_EQ(required_chunk_count, 27U);
}

TEST(GripperHmiDriverLogicTest, RejectsCalibrationHeaderWhenHeaderChunksMissingOrInvalid)
{
    std::array<uint8_t, gripper_hmi::kCalibrationPayloadSize> raw = {};
    gripper_hmi::GripperCalibrationHeader header{};
    std::memcpy(raw.data(), &header, sizeof(header));

    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received = {};
    received[0] = true;

    gripper_hmi::GripperCalibrationHeader parsed{};
    size_t required_chunk_count = 0;
    EXPECT_FALSE(gripper_hmi::driver_logic::TryParseCalibrationHeader(raw, received, &parsed, &required_chunk_count));

    received[1] = true;
    header.magic[0] = 'B';
    std::memcpy(raw.data(), &header, sizeof(header));
    EXPECT_FALSE(gripper_hmi::driver_logic::TryParseCalibrationHeader(raw, received, &parsed, &required_chunk_count));
}

TEST(GripperHmiDriverLogicTest, ChecksRequiredChunkCoverageAndSequentialFallbackGate)
{
    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received = {};
    received[0] = true;
    received[1] = true;
    received[2] = true;

    EXPECT_TRUE(gripper_hmi::driver_logic::HasAllCalibrationChunks(received, 3));
    EXPECT_FALSE(gripper_hmi::driver_logic::HasAllCalibrationChunks(received, 4));

    EXPECT_TRUE(gripper_hmi::driver_logic::ShouldAttemptSequentialCalibrationFallback(
        false, true, 4, received, GripperHmiProtocol::kStatusChecksumError));
    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldAttemptSequentialCalibrationFallback(
        true, true, 4, received, GripperHmiProtocol::kStatusChecksumError));
    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldAttemptSequentialCalibrationFallback(
        false, true, 3, received, GripperHmiProtocol::kStatusChecksumError));
    EXPECT_FALSE(gripper_hmi::driver_logic::ShouldAttemptSequentialCalibrationFallback(
        false, true, 4, received, GripperHmiProtocol::kStatusInvalidCommand));
}

TEST(GripperHmiDriverLogicTest, DecidesCalibrationRangeRetryActions)
{
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationRangeRetryAction(
            GripperHmiProtocol::kStatusMissingData, 0, 5),
        gripper_hmi::driver_logic::CalibrationReadRetryAction::AbortRecoverRetry);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationRangeRetryAction(
            GripperHmiProtocol::kStatusZeroDataChecksumError, 4, 5),
        gripper_hmi::driver_logic::CalibrationReadRetryAction::RetryAfterDelay);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationRangeRetryAction(
            GripperHmiProtocol::kStatusChecksumError, 1, 5),
        gripper_hmi::driver_logic::CalibrationReadRetryAction::RetryAfterDelay);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationRangeRetryAction(
            GripperHmiProtocol::kStatusInvalidCommand, 1, 5),
        gripper_hmi::driver_logic::CalibrationReadRetryAction::StopFailure);
}

TEST(GripperHmiDriverLogicTest, DecidesCalibrationWriteChunkStatusActions)
{
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationWriteChunkStatus(GripperHmiProtocol::kStatusOk, 0),
        CalibrationWriteChunkStatusAction::Ack);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationWriteChunkStatus(GripperHmiProtocol::kStatusMissingData, 0),
        CalibrationWriteChunkStatusAction::AbortAndRecover);
    EXPECT_EQ(gripper_hmi::driver_logic::DecideCalibrationWriteChunkStatus(
                  GripperHmiProtocol::kStatusZeroDataChecksumError, 0),
              CalibrationWriteChunkStatusAction::Retry);
    EXPECT_EQ(gripper_hmi::driver_logic::DecideCalibrationWriteChunkStatus(
                  GripperHmiProtocol::kStatusZeroDataChecksumError, 1),
              CalibrationWriteChunkStatusAction::AbortAndRecover);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationWriteChunkStatus(GripperHmiProtocol::kStatusChecksumError, 0),
        CalibrationWriteChunkStatusAction::Retry);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationWriteChunkStatus(GripperHmiProtocol::kStatusInvalidCommand, 0),
        CalibrationWriteChunkStatusAction::Fail);
}

TEST(GripperHmiDriverLogicTest, DecidesCalibrationChunkReadActions)
{
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationChunkReadAction(
            gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedData,
            GripperHmiProtocol::kCalibrationChunkSize,
            GripperHmiProtocol::kCalibrationChunkSize,
            GripperHmiProtocol::kStatusOk,
            0,
            8),
        gripper_hmi::driver_logic::CalibrationChunkReadAction::AcceptData);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationChunkReadAction(
            gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedStatus,
            0,
            GripperHmiProtocol::kCalibrationChunkSize,
            GripperHmiProtocol::kStatusMissingData,
            0,
            8),
        gripper_hmi::driver_logic::CalibrationChunkReadAction::AbortRecoverRetry);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationChunkReadAction(
            gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedStatus,
            0,
            GripperHmiProtocol::kCalibrationChunkSize,
            GripperHmiProtocol::kStatusZeroDataChecksumError,
            7,
            8),
        gripper_hmi::driver_logic::CalibrationChunkReadAction::RetryAfterDelay);
    EXPECT_EQ(
        gripper_hmi::driver_logic::DecideCalibrationChunkReadAction(
            gripper_hmi::driver_logic::ExclusiveFrameScanResult::NeedMoreData,
            0,
            GripperHmiProtocol::kCalibrationChunkSize,
            GripperHmiProtocol::kStatusOk,
            0,
            8),
        gripper_hmi::driver_logic::CalibrationChunkReadAction::StopFailure);
}

TEST(GripperHmiDriverLogicTest, NormalizesBufferToHeaderOrKeepsTrailingByte)
{
    std::vector<uint8_t> buffer = {0x10, 0x11, 0x12};
    EXPECT_FALSE(gripper_hmi::driver_logic::NormalizeBufferToRecvHeader(&buffer));
    ASSERT_EQ(buffer.size(), 1U);
    EXPECT_EQ(buffer[0], 0x12);

    buffer = {0x10, 0x11, GripperHmiProtocol::kRecvHead1, GripperHmiProtocol::kRecvHead2, 0x01};
    EXPECT_TRUE(gripper_hmi::driver_logic::NormalizeBufferToRecvHeader(&buffer));
    ASSERT_EQ(buffer.size(), 3U);
    EXPECT_EQ(buffer[0], GripperHmiProtocol::kRecvHead1);
    EXPECT_EQ(buffer[1], GripperHmiProtocol::kRecvHead2);
}

TEST(GripperHmiDriverLogicTest, ScansExpectedExclusiveStatusFrameWithSkipRules)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x42,
        0x01,
        0x02,
        0x03,
        0x04,
        0x00,
    };
    buffer[7] = GripperHmiProtocol::calculateXor(buffer.data(), 7);
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanExpectedExclusiveStatusFrame(&buffer, 0x52, nullptr, nullptr),
        gripper_hmi::driver_logic::StatusFrameScanResult::ContinueScanning);
    EXPECT_TRUE(buffer.empty());

    std::vector<uint8_t> matched = {
        0x00,
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x52,
        GripperHmiProtocol::kStatusOk,
        0x00,
        0x00,
        0xA5,
        0x00,
    };
    matched.back() = GripperHmiProtocol::calculateXor(matched.data() + 1, 7);
    uint8_t token = 0;
    uint8_t status = 0;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanExpectedExclusiveStatusFrame(&matched, 0x52, &token, &status),
        gripper_hmi::driver_logic::StatusFrameScanResult::MatchedStatus);
    EXPECT_EQ(token, 0x52);
    EXPECT_EQ(status, GripperHmiProtocol::kStatusOk);
}

TEST(GripperHmiDriverLogicTest, ScansCalibrationDataOrStatusFrameAcrossWrongTokenAndData)
{
    std::vector<uint8_t> wrong_token_status = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x51,
        GripperHmiProtocol::kStatusMissingData,
        0x00,
        0x00,
        0xA5,
        0x00,
    };
    wrong_token_status[7] = GripperHmiProtocol::calculateXor(wrong_token_status.data(), 7);

    std::vector<uint8_t> data_frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x52,
    };
    for (uint8_t i = 0; i < GripperHmiProtocol::kCalibrationChunkSize; ++i)
    {
        data_frame.push_back(i);
    }
    data_frame.push_back(0x00);
    data_frame.back() = GripperHmiProtocol::calculateXor(data_frame.data() + 3, GripperHmiProtocol::kCalibrationChunkSize);

    std::vector<uint8_t> buffer = wrong_token_status;
    buffer.insert(buffer.end(), data_frame.begin(), data_frame.end());

    std::vector<uint8_t> payload;
    uint8_t token = 0;
    uint8_t status = 0;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanCalibrationDataOrStatusFrame(&buffer, 0x52, GripperHmiProtocol::kCalibrationChunkSize,
                                                                    &payload, &token, &status),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::ContinueScanning);
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanCalibrationDataOrStatusFrame(&buffer, 0x52, GripperHmiProtocol::kCalibrationChunkSize,
                                                                    &payload, &token, &status),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedData);
    ASSERT_EQ(payload.size(), GripperHmiProtocol::kCalibrationChunkSize);
    EXPECT_EQ(payload.front(), 0);
    EXPECT_EQ(payload.back(), GripperHmiProtocol::kCalibrationChunkSize - 1);
}

TEST(GripperHmiDriverLogicTest, ScansRawDataOrStatusFrameWithOversizedGarbageDrop)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x99,
        0x88,
        0x77,
        0x66,
        0x55,
        0x44,
        0x33,
    };

    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanRawDataOrStatusFrame(&buffer, 4, nullptr, nullptr, nullptr),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::ContinueScanning);
    ASSERT_EQ(buffer.front(), GripperHmiProtocol::kRecvHead2);

    std::vector<uint8_t> status_frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x57,
        GripperHmiProtocol::kStatusChecksumError,
        0x00,
        0x00,
        0xA5,
        0x00,
    };
    status_frame[7] = GripperHmiProtocol::calculateXor(status_frame.data(), 7);
    uint8_t token = 0;
    uint8_t status = 0;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanRawDataOrStatusFrame(&status_frame, 4, nullptr, &token, &status),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedStatus);
    EXPECT_EQ(token, 0x57);
    EXPECT_EQ(status, GripperHmiProtocol::kStatusChecksumError);
}

TEST(GripperHmiDriverLogicTest, ScansSizedFrameAcrossBadChecksumThenValidFrame)
{
    std::vector<uint8_t> invalid_frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x41,
        0x42,
        0x43,
        0x00,
    };
    invalid_frame[5] = 0x5A;

    std::vector<uint8_t> valid_frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x51,
        0x52,
        0x53,
        0x00,
    };
    valid_frame[5] = GripperHmiProtocol::calculateXor(valid_frame.data(), 5);

    std::vector<uint8_t> buffer = invalid_frame;
    buffer.insert(buffer.end(), valid_frame.begin(), valid_frame.end());

    std::vector<uint8_t> frame;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanSizedRecvFrame(&buffer, valid_frame.size(), &frame),
        gripper_hmi::driver_logic::SizedFrameScanResult::ContinueScanning);
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanSizedRecvFrame(&buffer, valid_frame.size(), &frame),
        gripper_hmi::driver_logic::SizedFrameScanResult::MatchedFrame);
    EXPECT_EQ(frame, valid_frame);
}

TEST(GripperHmiDriverLogicTest, ScansExpectedCalibrationFrameAcrossWrongTokenAndGarbage)
{
    std::vector<uint8_t> wrong_token_frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x20,
    };
    for (uint8_t i = 0; i < GripperHmiProtocol::kCalibrationChunkSize; ++i)
    {
        wrong_token_frame.push_back(i);
    }
    wrong_token_frame.push_back(0x00);
    wrong_token_frame.back() = GripperHmiProtocol::calculateXor(
        wrong_token_frame.data() + 3, GripperHmiProtocol::kCalibrationChunkSize);

    std::vector<uint8_t> valid_frame = {
        0xFF,
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x21,
    };
    for (uint8_t i = 0; i < GripperHmiProtocol::kCalibrationChunkSize; ++i)
    {
        valid_frame.push_back(static_cast<uint8_t>(i + 1));
    }
    valid_frame.push_back(0x00);
    valid_frame.back() = GripperHmiProtocol::calculateXor(
        valid_frame.data() + 4, GripperHmiProtocol::kCalibrationChunkSize);

    std::vector<uint8_t> buffer = wrong_token_frame;
    buffer.insert(buffer.end(), valid_frame.begin(), valid_frame.end());

    std::vector<uint8_t> payload;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanExpectedCalibrationFrame(&buffer, 0x21, &payload),
        gripper_hmi::driver_logic::CalibrationFrameScanResult::ContinueScanning);
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanExpectedCalibrationFrame(&buffer, 0x21, &payload),
        gripper_hmi::driver_logic::CalibrationFrameScanResult::MatchedFrame);
    ASSERT_EQ(payload.size(), GripperHmiProtocol::kCalibrationChunkSize);
    EXPECT_EQ(payload.front(), 1);
    EXPECT_EQ(payload.back(), GripperHmiProtocol::kCalibrationChunkSize);
}

TEST(GripperHmiDriverLogicTest, ScansSizedFrameKeepsTrailingByteWhenHeaderMissing)
{
    std::vector<uint8_t> buffer = {0x31, 0x32, 0x33};
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanSizedRecvFrame(&buffer, 6, nullptr),
        gripper_hmi::driver_logic::SizedFrameScanResult::NeedMoreData);
    ASSERT_EQ(buffer.size(), 1U);
    EXPECT_EQ(buffer.front(), 0x33);
}

TEST(GripperHmiDriverLogicTest, EvaluatesExclusiveDrainStepForIdleExtensionAndCutoff)
{
    const auto continue_after_bytes = gripper_hmi::driver_logic::EvaluateExclusiveDrainStep(100, 140, 220, 8, 40);
    EXPECT_EQ(continue_after_bytes.action, gripper_hmi::driver_logic::ExclusiveDrainAction::ContinueAfterBytes);
    EXPECT_EQ(continue_after_bytes.next_idle_deadline_ms, 140);

    const auto continue_sleeping = gripper_hmi::driver_logic::EvaluateExclusiveDrainStep(120, 160, 220, 0, 40);
    EXPECT_EQ(continue_sleeping.action, gripper_hmi::driver_logic::ExclusiveDrainAction::ContinueSleeping);
    EXPECT_EQ(continue_sleeping.next_idle_deadline_ms, 160);

    const auto drained_by_idle = gripper_hmi::driver_logic::EvaluateExclusiveDrainStep(161, 160, 260, 0, 40);
    EXPECT_EQ(drained_by_idle.action, gripper_hmi::driver_logic::ExclusiveDrainAction::StopDrained);

    const auto drained_by_max = gripper_hmi::driver_logic::EvaluateExclusiveDrainStep(220, 260, 220, 4, 40);
    EXPECT_EQ(drained_by_max.action, gripper_hmi::driver_logic::ExclusiveDrainAction::StopDrained);

    const auto io_failure = gripper_hmi::driver_logic::EvaluateExclusiveDrainStep(120, 160, 220, -1, 40);
    EXPECT_EQ(io_failure.action, gripper_hmi::driver_logic::ExclusiveDrainAction::StopIoFailure);
}

TEST(GripperHmiDriverLogicTest, EvaluatesReadBytesStepForDataTimeoutAndIoFailure)
{
    EXPECT_EQ(
        gripper_hmi::driver_logic::EvaluateReadBytesStep(false, 0),
        gripper_hmi::driver_logic::ReadBytesAction::ContinueSleeping);
    EXPECT_EQ(
        gripper_hmi::driver_logic::EvaluateReadBytesStep(false, 7),
        gripper_hmi::driver_logic::ReadBytesAction::ReturnSuccessWithData);
    EXPECT_EQ(
        gripper_hmi::driver_logic::EvaluateReadBytesStep(true, 0),
        gripper_hmi::driver_logic::ReadBytesAction::ReturnSuccessNoData);
    EXPECT_EQ(
        gripper_hmi::driver_logic::EvaluateReadBytesStep(false, -1),
        gripper_hmi::driver_logic::ReadBytesAction::ReturnIoFailure);
}

TEST(GripperHmiDriverLogicTest, CollectsCalibrationRangeChunkAndParsesHeaderCompletion)
{
    std::array<uint8_t, gripper_hmi::kCalibrationPayloadSize> raw = {};
    gripper_hmi::GripperCalibrationHeader header{};
    header.payloadSize =
        GripperHmiProtocol::kCalibrationChunkSize * gripper_hmi::driver_logic::kCalibrationHeaderChunkCount;
    std::memcpy(raw.data(), &header, sizeof(header));

    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received = {};
    for (size_t i = 0; i + 1 < gripper_hmi::driver_logic::kCalibrationHeaderChunkCount; ++i)
    {
        received[i] = true;
    }

    const uint8_t token = static_cast<uint8_t>(gripper_hmi::driver_logic::kCalibrationHeaderChunkCount - 1);
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        token,
    };
    const size_t chunk_offset = static_cast<size_t>(token) * GripperHmiProtocol::kCalibrationChunkSize;
    buffer.insert(buffer.end(),
                  raw.begin() + static_cast<std::ptrdiff_t>(chunk_offset),
                  raw.begin() + static_cast<std::ptrdiff_t>(chunk_offset + GripperHmiProtocol::kCalibrationChunkSize));
    buffer.push_back(0x00);
    buffer.back() = GripperHmiProtocol::calculateXor(
        buffer.data() + 3, GripperHmiProtocol::kCalibrationChunkSize);

    bool required_chunk_count_known = false;
    size_t required_chunk_count = gripper_hmi::driver_logic::kCalibrationChunkCount;
    uint8_t status_token = 0;
    uint8_t status_code = 0;
    EXPECT_EQ(
        gripper_hmi::driver_logic::CollectCalibrationRangeFrame(
            &buffer,
            gripper_hmi::driver_logic::CalibrationRangeCollectState{
                .raw = &raw,
                .received = &received,
                .required_chunk_count_known = &required_chunk_count_known,
                .required_chunk_count = &required_chunk_count,
                .status_token = &status_token,
                .status_code = &status_code,
            }),
        gripper_hmi::driver_logic::CalibrationRangeCollectResult::CollectionComplete);
    EXPECT_TRUE(required_chunk_count_known);
    EXPECT_EQ(required_chunk_count, gripper_hmi::driver_logic::kCalibrationHeaderChunkCount);
    EXPECT_TRUE(received[token]);
    EXPECT_TRUE(buffer.empty());
}

TEST(GripperHmiDriverLogicTest, CollectCalibrationRangeFrameConsumesStatusAndDropsGarbage)
{
    std::array<uint8_t, gripper_hmi::kCalibrationPayloadSize> raw = {};
    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received = {};
    bool required_chunk_count_known = false;
    size_t required_chunk_count = gripper_hmi::driver_logic::kCalibrationChunkCount;
    uint8_t status_token = 0;
    uint8_t status_code = 0;

    std::vector<uint8_t> status_frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x44,
        GripperHmiProtocol::kStatusChecksumError,
        0x00,
        0x00,
        0xA5,
        0x00,
    };
    status_frame[7] = GripperHmiProtocol::calculateXor(status_frame.data(), 7);
    EXPECT_EQ(
        gripper_hmi::driver_logic::CollectCalibrationRangeFrame(
            &status_frame,
            gripper_hmi::driver_logic::CalibrationRangeCollectState{
                .raw = &raw,
                .received = &received,
                .required_chunk_count_known = &required_chunk_count_known,
                .required_chunk_count = &required_chunk_count,
                .status_token = &status_token,
                .status_code = &status_code,
            }),
        gripper_hmi::driver_logic::CalibrationRangeCollectResult::ContinueScanning);
    EXPECT_EQ(status_token, 0x44);
    EXPECT_EQ(status_code, GripperHmiProtocol::kStatusChecksumError);
    EXPECT_TRUE(status_frame.empty());

    std::vector<uint8_t> garbage = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x99,
        0x88,
        0x77,
        0x66,
        0x55,
        0x44,
        0x33,
        0x22,
        0x11,
        0x00,
        0xFE,
        0xED,
        0xDC,
        0xCB,
        0xBA,
        0xA9,
        0x98,
        0x87,
        0x76,
        0x65,
        0x54,
        0x43,
        0x32,
        0x21,
        0x10,
        0x0F,
        0xEE,
        0xDD,
        0xCC,
        0xBB,
        0xAA,
        0x99,
        0x88,
        0x77,
    };
    EXPECT_EQ(
        gripper_hmi::driver_logic::CollectCalibrationRangeFrame(
            &garbage,
            gripper_hmi::driver_logic::CalibrationRangeCollectState{
                .raw = &raw,
                .received = &received,
                .required_chunk_count_known = &required_chunk_count_known,
                .required_chunk_count = &required_chunk_count,
                .status_token = &status_token,
                .status_code = &status_code,
            }),
        gripper_hmi::driver_logic::CalibrationRangeCollectResult::ContinueScanning);
    ASSERT_FALSE(garbage.empty());
    EXPECT_EQ(garbage.front(), GripperHmiProtocol::kRecvHead2);
}



TEST(GripperHmiDriverLogicTest, ScansTwoCoalescedRawFramesWithoutDroppingTail)
{
    auto appendFrame = [](std::vector<uint8_t>* buffer, uint8_t fill) {
        const size_t start = buffer->size();
        buffer->push_back(GripperHmiProtocol::kRecvHead1);
        buffer->push_back(GripperHmiProtocol::kRecvHead2);
        buffer->insert(buffer->end(), GripperHmiProtocol::kSerialNumberChunkSize, fill);
        buffer->push_back(GripperHmiProtocol::calculateXor(
            buffer->data() + start,
            2 + GripperHmiProtocol::kSerialNumberChunkSize));
    };

    std::vector<uint8_t> buffer;
    appendFrame(&buffer, 0x31);
    appendFrame(&buffer, 0x32);

    std::vector<uint8_t> first;
    std::vector<uint8_t> second;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanRawDataOrStatusFrame(
            &buffer, GripperHmiProtocol::kSerialNumberChunkSize, &first, nullptr, nullptr),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedData);
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanRawDataOrStatusFrame(
            &buffer, GripperHmiProtocol::kSerialNumberChunkSize, &second, nullptr, nullptr),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedData);
    EXPECT_TRUE(buffer.empty());
    ASSERT_EQ(first.size(), GripperHmiProtocol::kSerialNumberChunkSize);
    ASSERT_EQ(second.size(), GripperHmiProtocol::kSerialNumberChunkSize);
    EXPECT_EQ(first.front(), 0x31);
    EXPECT_EQ(second.front(), 0x32);
}

TEST(GripperHmiDriverLogicTest, AcceptsOneBasedCalibrationResponseTokenForSingleChunkRead)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x01,
    };
    for (uint8_t i = 0; i < GripperHmiProtocol::kCalibrationChunkSize; ++i)
    {
        buffer.push_back(static_cast<uint8_t>(0xA0 + i));
    }
    buffer.push_back(GripperHmiProtocol::calculateXor(
        buffer.data() + 3, GripperHmiProtocol::kCalibrationChunkSize));

    std::vector<uint8_t> payload;
    uint8_t token = 0;
    uint8_t status = 0;
    EXPECT_EQ(
        gripper_hmi::driver_logic::ScanCalibrationDataOrStatusFrame(
            &buffer, 0, GripperHmiProtocol::kCalibrationChunkSize, &payload, &token, &status),
        gripper_hmi::driver_logic::ExclusiveFrameScanResult::MatchedData);
    ASSERT_EQ(payload.size(), GripperHmiProtocol::kCalibrationChunkSize);
    EXPECT_EQ(payload.front(), 0xA0);
    EXPECT_EQ(payload.back(), 0xAF);
}

TEST(GripperHmiDriverLogicTest, CollectsCompleteOneBasedRawCalibrationPayloadWithoutValidHeader)
{
    gripper_hmi::GripperCalibrationPayload raw{};
    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received{};
    bool required_chunk_count_known = true;
    size_t required_chunk_count = gripper_hmi::driver_logic::kCalibrationChunkCount;
    auto index_base = gripper_hmi::driver_logic::CalibrationChunkIndexBase::Unknown;
    uint8_t status_token = 0;
    uint8_t status_code = 0;

    for (size_t packet_index = 0;
         packet_index < gripper_hmi::driver_logic::kCalibrationChunkCount;
         ++packet_index)
    {
        const uint8_t token = static_cast<uint8_t>(packet_index + 1);
        std::vector<uint8_t> frame = {
            GripperHmiProtocol::kRecvHead1,
            GripperHmiProtocol::kRecvHead2,
            token,
        };
        frame.insert(frame.end(),
                     GripperHmiProtocol::kCalibrationChunkSize,
                     static_cast<uint8_t>(packet_index));
        frame.push_back(GripperHmiProtocol::calculateXor(
            frame.data() + 3, GripperHmiProtocol::kCalibrationChunkSize));

        const auto result = gripper_hmi::driver_logic::CollectCalibrationRangeFrame(
            &frame,
            gripper_hmi::driver_logic::CalibrationRangeCollectState{
                .raw = &raw,
                .received = &received,
                .required_chunk_count_known = &required_chunk_count_known,
                .required_chunk_count = &required_chunk_count,
                .index_base = &index_base,
                .status_token = &status_token,
                .status_code = &status_code,
            });
        EXPECT_EQ(
            result,
            packet_index + 1 == gripper_hmi::driver_logic::kCalibrationChunkCount
                ? gripper_hmi::driver_logic::CalibrationRangeCollectResult::CollectionComplete
                : gripper_hmi::driver_logic::CalibrationRangeCollectResult::ContinueScanning);
    }

    EXPECT_EQ(index_base, gripper_hmi::driver_logic::CalibrationChunkIndexBase::OneBased);
    EXPECT_TRUE(gripper_hmi::driver_logic::HasAllCalibrationChunks(received, required_chunk_count));
    EXPECT_EQ(raw.front(), 0x00);
    EXPECT_EQ(raw[GripperHmiProtocol::kCalibrationChunkSize], 0x01);
    EXPECT_EQ(raw.back(), 0x3F);
    EXPECT_TRUE(received.front());
    EXPECT_TRUE(received.back());
}

TEST(GripperHmiDriverLogicTest, CollectsZeroBasedCalibrationRangePayload)
{
    gripper_hmi::GripperCalibrationPayload raw{};
    std::array<bool, gripper_hmi::driver_logic::kCalibrationChunkCount> received{};
    bool required_chunk_count_known = true;
    size_t required_chunk_count = gripper_hmi::driver_logic::kCalibrationChunkCount;
    auto index_base = gripper_hmi::driver_logic::CalibrationChunkIndexBase::Unknown;

    std::vector<uint8_t> frame = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        0x00,
    };
    frame.insert(frame.end(), GripperHmiProtocol::kCalibrationChunkSize, 0x5A);
    frame.push_back(GripperHmiProtocol::calculateXor(
        frame.data() + 3, GripperHmiProtocol::kCalibrationChunkSize));

    EXPECT_EQ(
        gripper_hmi::driver_logic::CollectCalibrationRangeFrame(
            &frame,
            gripper_hmi::driver_logic::CalibrationRangeCollectState{
                .raw = &raw,
                .received = &received,
                .required_chunk_count_known = &required_chunk_count_known,
                .required_chunk_count = &required_chunk_count,
                .index_base = &index_base,
            }),
        gripper_hmi::driver_logic::CalibrationRangeCollectResult::ContinueScanning);
    EXPECT_EQ(index_base, gripper_hmi::driver_logic::CalibrationChunkIndexBase::ZeroBased);
    EXPECT_TRUE(received[0]);
    EXPECT_EQ(raw[0], 0x5A);
    EXPECT_EQ(raw[GripperHmiProtocol::kCalibrationChunkSize - 1], 0x5A);
}

}  // namespace
