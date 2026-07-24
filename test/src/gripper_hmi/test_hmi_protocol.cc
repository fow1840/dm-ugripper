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

TEST(GripperHmiProtocolTest, TryConsumeFrameParsesPressureReportAsBigEndianValue)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        GripperHmiProtocol::kIndexPressureReport,
        0x12,
        0x34,
        0x56,
        0x78,
        0x00,
    };
    buffer.back() = GripperHmiProtocol::calculateXor(buffer.data(), GripperHmiProtocol::kFrameLength - 1);

    const auto frame = GripperHmiProtocol::tryConsumeFrame(buffer);

    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->type, GripperFrameType::PressureReport);
    EXPECT_EQ(frame->pressureReport.raw, (std::array<uint8_t, 4>{0x12, 0x34, 0x56, 0x78}));
    EXPECT_EQ(frame->pressureReport.value, 0x12345678);
}

TEST(GripperHmiProtocolTest, TryConsumeFrameParsesNegativePressureReport)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        GripperHmiProtocol::kIndexPressureReport,
        0xFF,
        0xFF,
        0xEC,
        0x36,
        0x00,
    };
    buffer.back() = GripperHmiProtocol::calculateXor(buffer.data(), GripperHmiProtocol::kFrameLength - 1);

    const auto frame = GripperHmiProtocol::tryConsumeFrame(buffer);

    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->type, GripperFrameType::PressureReport);
    EXPECT_EQ(frame->pressureReport.value, -5066);
}

TEST(GripperHmiProtocolTest, BuildPressureRequestUsesGetFunctionAndExpectedChecksum)
{
    const auto frame = GripperHmiProtocol::buildPressureRequest();

    EXPECT_EQ(frame, (std::array<uint8_t, 7>{0x5A, 0x5A, 0x30, 0x02, 0x00, 0x00, 0x32}));
}

TEST(GripperHmiProtocolTest, ForceCalibrationPayloadUsesExistingSerialEncoding)
{
    gripper_hmi::GripperCalibrationData data{};
    ASSERT_TRUE(gripper_hmi::makeForceCalibrationData(
        "DAGY12267600014A", -5000, 495000, 1000000, 1784800000, &data));

    EXPECT_EQ(data.header.dataFormatVersion, gripper_hmi::kCalibrationDataFormatVersionForceV1);
    EXPECT_EQ(data.header.payloadSize, 96);
    EXPECT_EQ(gripper_hmi::decodeSerialNumber(data.force.serialNumber), "DAGY12267600014A");
    EXPECT_TRUE(gripper_hmi::isForceCalibrationPayload(data));
    EXPECT_TRUE(gripper_hmi::forceCalibrationMatchesSerialNumber(data, "DAGY12267600014A"));
    EXPECT_FALSE(gripper_hmi::forceCalibrationMatchesSerialNumber(data, "DAGZ12267600016B"));
}

TEST(GripperHmiProtocolTest, ForceCalibrationUsesSignedReferenceDelta)
{
    gripper_hmi::GripperCalibrationData data{};
    ASSERT_TRUE(gripper_hmi::makeForceCalibrationData(
        "DAGZ12267600016B", 1000, -499000, 1000000, 1784800000, &data));

    int64_t massMg = 0;
    ASSERT_TRUE(gripper_hmi::forceMassMg(data, -249000, &massMg));
    EXPECT_EQ(massMg, 500000);
}

TEST(GripperHmiProtocolTest, ForceCalibrationCrcRejectsMutatedPayload)
{
    gripper_hmi::GripperCalibrationData data{};
    ASSERT_TRUE(gripper_hmi::makeForceCalibrationData(
        "DAGY12267600014A", 0, 500000, 1000000, 1784800000, &data));
    data.force.referenceRaw += 1;

    EXPECT_FALSE(gripper_hmi::isForceCalibrationPayload(data));
}

TEST(GripperHmiProtocolTest, TryConsumeFrameParsesAmbientLightLux)
{
    std::vector<uint8_t> buffer = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
        GripperHmiProtocol::kIndexAmbientLightReport,
        0x00,
        0x00,
        0x03,
        0xE8,
        0x00,
    };
    buffer.back() = GripperHmiProtocol::calculateXor(buffer.data(), GripperHmiProtocol::kFrameLength - 1);

    const auto frame = GripperHmiProtocol::tryConsumeFrame(buffer);

    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->type, GripperFrameType::AmbientLightReport);
    EXPECT_EQ(frame->ambientLightReport.lux, 1000U);
}

TEST(GripperHmiProtocolTest, BuildAmbientLightRequestUsesGetFunctionAndExpectedChecksum)
{
    const auto frame = GripperHmiProtocol::buildAmbientLightRequest();

    EXPECT_EQ(frame, (std::array<uint8_t, 7>{0x5A, 0x5A, 0x31, 0x02, 0x00, 0x00, 0x33}));
}

TEST(GripperHmiProtocolTest, BuildSetFillLightPwmCommandUsesExpectedFrameAndChecksum)
{
    const auto frame = GripperHmiProtocol::buildSetFillLightPwmCommand(500);

    EXPECT_EQ(frame[0], GripperHmiProtocol::kSendHead1);
    EXPECT_EQ(frame[1], GripperHmiProtocol::kSendHead2);
    EXPECT_EQ(frame[2], GripperHmiProtocol::kIndexFillLight);
    EXPECT_EQ(frame[3], GripperHmiProtocol::kFuncFillLightPwm);
    EXPECT_EQ(frame[4], 0x01);
    EXPECT_EQ(frame[5], 0xF4);
    EXPECT_TRUE(GripperHmiProtocol::validateXor(frame.data(), frame.size() - 1, frame.back()));
}

TEST(GripperHmiProtocolTest, BuildSetFillLightPwmCommandSupportsFullDutyRangeAndClampsOverflow)
{
    const auto maximum = GripperHmiProtocol::buildSetFillLightPwmCommand(1000);
    const auto overflow = GripperHmiProtocol::buildSetFillLightPwmCommand(1001);

    EXPECT_EQ(maximum[4], 0x03);
    EXPECT_EQ(maximum[5], 0xE8);
    EXPECT_EQ(overflow, maximum);
}

}  // namespace
