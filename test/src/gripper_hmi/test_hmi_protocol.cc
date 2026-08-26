#include "gripper_hmi_protocol.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

TEST(GripperHmiProtocolTest, BuildSetBeepCommandUsesFreeIndexAndClampsDuty)
{
    const GripperBeepState state{300, 0x1234};
    const auto frame = GripperHmiProtocol::buildSetBeepCommand(state);

    EXPECT_EQ(frame[0], GripperHmiProtocol::kSendHead1);
    EXPECT_EQ(frame[1], GripperHmiProtocol::kSendHead2);
    EXPECT_EQ(frame[2], GripperHmiProtocol::kIndexBeepFree);
    EXPECT_EQ(frame[3], 0xFF);
    EXPECT_EQ(frame[4], 0x12);
    EXPECT_EQ(frame[5], 0x34);
    EXPECT_TRUE(GripperHmiProtocol::validateXor(frame.data(), frame.size() - 1, frame.back()));
}

TEST(GripperHmiProtocolTest, TryConsumeFrameSkipsNoiseAndParsesKeyReport)
{
    std::vector<uint8_t> buffer = {
        0x00,
        0x7F,
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        GripperHmiProtocol::kIndexKeyReport,
        0x00,
        0x00,
        0x00,
        0x02,
        0x00,
    };
    buffer.back() = GripperHmiProtocol::calculateXor(buffer.data() + 2, GripperHmiProtocol::kFrameLength - 1);

    const auto frame = GripperHmiProtocol::tryConsumeFrame(buffer);

    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->type, GripperFrameType::KeyReport);
    EXPECT_EQ(frame->keyReport.rawCode, 0x02);
    EXPECT_EQ(frame->keyReport.keyIndex, 1);
    EXPECT_TRUE(frame->keyReport.pressed);
    EXPECT_TRUE(buffer.empty());
}

TEST(GripperHmiProtocolTest, DescribeStatusCodeKeepsStableKnownStrings)
{
    EXPECT_EQ(GripperHmiProtocol::describeStatusCode(GripperHmiProtocol::kStatusOk), "ok");
    EXPECT_EQ(GripperHmiProtocol::describeStatusCode(GripperHmiProtocol::kStatusChecksumError), "checksum_error");
}

TEST(GripperHmiProtocolTest, TryConsumeFrameParsesReleaseKeyReport)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        GripperHmiProtocol::kIndexKeyReport,
        0x00,
        0x00,
        0x00,
        0x0A,
        0x00,
    };
    buffer.back() = GripperHmiProtocol::calculateXor(buffer.data(), GripperHmiProtocol::kFrameLength - 1);

    const auto frame = GripperHmiProtocol::tryConsumeFrame(buffer);

    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->type, GripperFrameType::KeyReport);
    EXPECT_EQ(frame->keyReport.keyIndex, 0);
    EXPECT_FALSE(frame->keyReport.pressed);
}

TEST(GripperHmiProtocolTest, TryConsumeFrameParsesBeepState)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        GripperHmiProtocol::kIndexBeepBase,
        0x00,
        0x32,
        0x0F,
        0xA0,
        0x00,
    };
    buffer.back() = GripperHmiProtocol::calculateXor(buffer.data(), GripperHmiProtocol::kFrameLength - 1);

    const auto frame = GripperHmiProtocol::tryConsumeFrame(buffer);

    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->type, GripperFrameType::BeepState);
    EXPECT_EQ(frame->beepState.duty, 0x0032);
    EXPECT_EQ(frame->beepState.frequency, 0x0FA0);
}

TEST(GripperHmiProtocolTest, BuildsVersionInitializationCommand)
{
    const auto initialize = GripperHmiProtocol::buildInitializeVersionCommand();
    const std::array<uint8_t, 7> expected = {0x5A, 0x5A, 0x57, 0x56, 0x65, 0x72, 0x16};
    EXPECT_EQ(initialize, expected);
}

TEST(GripperHmiProtocolTest, BuildsVersionReadCommands)
{
    const auto read56 = GripperHmiProtocol::buildReadVersionCommand();
    const std::array<uint8_t, 7> expectedRead56 = {0x5A, 0x5A, 0x52, 0x56, 0x00, 0x00, 0x04};
    EXPECT_EQ(read56, expectedRead56);

    const auto read57 = GripperHmiProtocol::buildReadFirmwareVersionCommand();
    const std::array<uint8_t, 7> expectedRead57 = {0x5A, 0x5A, 0x52, 0x57, 0x00, 0x00, 0x05};
    EXPECT_EQ(read57, expectedRead57);
}

TEST(GripperHmiProtocolTest, DecodesObservedRawVersionPayloads)
{
    const std::array<uint8_t, GripperHmiProtocol::kVersionDataSize> version56Payload = {
        'V', '1', '.', '2', '.', '1', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    const auto version56 = GripperHmiProtocol::decodeVersionPayload(
        version56Payload.data(), version56Payload.size());
    ASSERT_TRUE(version56.has_value());
    EXPECT_EQ(*version56, "V1.2.1");

    const std::array<uint8_t, GripperHmiProtocol::kVersionDataSize> version57Payload = {
        'V', '1', '.', '0', '.', '0', '2', '.', '2', '0', '2', '6', '0', '3', '2', '4',
    };
    const auto version57 = GripperHmiProtocol::decodeVersionPayload(
        version57Payload.data(), version57Payload.size());
    ASSERT_TRUE(version57.has_value());
    EXPECT_EQ(*version57, "V1.0.02.20260324");
}

TEST(GripperHmiProtocolTest, RejectsInvalidVersionPayload)
{
    std::array<uint8_t, GripperHmiProtocol::kVersionDataSize> payload = {};
    EXPECT_FALSE(GripperHmiProtocol::decodeVersionPayload(payload.data(), payload.size()).has_value());

    payload[0] = 'V';
    payload[1] = '1';
    payload[2] = 0x01;
    EXPECT_FALSE(GripperHmiProtocol::decodeVersionPayload(payload.data(), payload.size()).has_value());
}

}  // namespace
