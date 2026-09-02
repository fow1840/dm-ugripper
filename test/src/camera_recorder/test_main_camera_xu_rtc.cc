#include "camera_recorder/main_camera_xu_rtc.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr std::array<uint8_t, 16> kUuid = {
    0x8f, 0xe5, 0x4d, 0x3a, 0x21, 0x7c, 0x4b, 0x6f,
    0x93, 0x2d, 0x50, 0x54, 0x53, 0x48, 0x32, 0x35,
};

void AppendBigEndian64(std::vector<uint8_t>* output, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output->push_back(static_cast<uint8_t>(value >> shift));
    }
}

std::vector<uint8_t> EscapeRbsp(const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> result;
    unsigned int zero_count = 0;
    for (const uint8_t value : rbsp) {
        if (zero_count >= 2 && value <= 0x03) {
            result.push_back(0x03);
            zero_count = 0;
        }
        result.push_back(value);
        if (value == 0) {
            ++zero_count;
        } else {
            zero_count = 0;
        }
    }
    return result;
}

std::vector<uint8_t> MakeSeiNal(uint64_t encoder_pts_us,
                                uint64_t xu_time_us,
                                bool valid_marker = true) {
    std::vector<uint8_t> rbsp;
    rbsp.push_back(5);
    rbsp.push_back(36);
    rbsp.insert(rbsp.end(), kUuid.begin(), kUuid.end());
    AppendBigEndian64(&rbsp, encoder_pts_us);
    AppendBigEndian64(&rbsp, xu_time_us);
    rbsp.push_back('X');
    rbsp.push_back('U');
    rbsp.push_back(valid_marker ? '2' : '1');
    rbsp.push_back(0);
    rbsp.push_back(0x80);

    std::vector<uint8_t> nal = {0x50, 0x01};  // H.265 suffix SEI, NAL type 40.
    const auto escaped = EscapeRbsp(rbsp);
    nal.insert(nal.end(), escaped.begin(), escaped.end());
    return nal;
}

TEST(MainCameraXuRtcTest, ParsesAnnexBSuffixSei) {
    constexpr uint64_t kEncoderPtsUs = 123'456;
    constexpr uint64_t kXuTimeUs = 1'788'161'400'000'000;
    const auto nal = MakeSeiNal(kEncoderPtsUs, kXuTimeUs);

    std::vector<uint8_t> access_unit = {0, 0, 0, 1};
    access_unit.insert(access_unit.end(), nal.begin(), nal.end());

    const auto timestamp = ugripper::camera::ParseMainCameraXuTimestamp(
        access_unit.data(), access_unit.size());
    ASSERT_TRUE(timestamp.has_value());
    EXPECT_EQ(timestamp->encoder_pts_us, kEncoderPtsUs);
    EXPECT_EQ(timestamp->xu_time_us, kXuTimeUs);
}

TEST(MainCameraXuRtcTest, ParsesLengthPrefixedSuffixSei) {
    const auto nal = MakeSeiNal(50'000, 1'700'000'000'000'000);
    const uint32_t nal_size = static_cast<uint32_t>(nal.size());
    std::vector<uint8_t> access_unit = {
        static_cast<uint8_t>(nal_size >> 24),
        static_cast<uint8_t>(nal_size >> 16),
        static_cast<uint8_t>(nal_size >> 8),
        static_cast<uint8_t>(nal_size),
    };
    access_unit.insert(access_unit.end(), nal.begin(), nal.end());

    const auto timestamp = ugripper::camera::ParseMainCameraXuTimestamp(
        access_unit.data(), access_unit.size());
    ASSERT_TRUE(timestamp.has_value());
    EXPECT_EQ(timestamp->encoder_pts_us, 50'000U);
    EXPECT_EQ(timestamp->xu_time_us, 1'700'000'000'000'000U);
}

TEST(MainCameraXuRtcTest, PreservesDataByteAfterEmulationPreventionByte) {
    constexpr uint64_t kEncoderPtsUs = 0x0000030000000000ULL;
    const auto nal = MakeSeiNal(kEncoderPtsUs, 1'700'000'000'000'000);
    std::vector<uint8_t> access_unit = {0, 0, 0, 1};
    access_unit.insert(access_unit.end(), nal.begin(), nal.end());

    const auto timestamp = ugripper::camera::ParseMainCameraXuTimestamp(
        access_unit.data(), access_unit.size());
    ASSERT_TRUE(timestamp.has_value());
    EXPECT_EQ(timestamp->encoder_pts_us, kEncoderPtsUs);
}

TEST(MainCameraXuRtcTest, RejectsWrongMarker) {
    const auto nal = MakeSeiNal(1, 2, false);
    std::vector<uint8_t> access_unit = {0, 0, 1};
    access_unit.insert(access_unit.end(), nal.begin(), nal.end());
    EXPECT_FALSE(ugripper::camera::ParseMainCameraXuTimestamp(
                     access_unit.data(), access_unit.size())
                     .has_value());
}

TEST(MainCameraXuRtcTest, ConvertsCameraTimestampToUnixTime) {
    const ugripper::camera::MainCameraXuTimestamp timestamp{
        .encoder_pts_us = 250'000,
        .xu_time_us = 1'700'000'000'000'000,
    };
    EXPECT_EQ(ugripper::camera::MainCameraFrameUnixTimeUs(timestamp, 200'000),
              1'700'000'000'050'000);
}

TEST(MainCameraXuRtcTest, MapsSetEncoderPtsToWrittenUnixTime) {
    const ugripper::camera::MainCameraXuTimestamp first{
        .encoder_pts_us = 73'764'647,
        .xu_time_us = 1'788'161'400'000'000,
    };
    EXPECT_EQ(ugripper::camera::MainCameraFrameUnixTimeUs(
                  first, first.encoder_pts_us),
              1'788'161'400'000'000);
}

TEST(MainCameraXuRtcTest, RejectsEncoderPtsBeforePset) {
    const ugripper::camera::MainCameraXuTimestamp timestamp{
        .encoder_pts_us = 99,
        .xu_time_us = 1'700'000'000'000'000,
    };
    EXPECT_FALSE(ugripper::camera::MainCameraFrameUnixTimeUs(timestamp, 100)
                     .has_value());
}

TEST(MainCameraXuRtcTest, RejectsUnixTimeOverflow) {
    const ugripper::camera::MainCameraXuTimestamp timestamp{
        .encoder_pts_us = 20,
        .xu_time_us = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - 5,
    };
    EXPECT_FALSE(ugripper::camera::MainCameraFrameUnixTimeUs(timestamp, 10)
                     .has_value());
}

TEST(MainCameraXuRtcTest, AppliesSingleRtcWriteAheadConstant) {
    constexpr int64_t kNowUs = 1'700'000'000'000'000;
    EXPECT_EQ(ugripper::camera::MainCameraRtcWriteTimeUs(kNowUs),
              kNowUs + ugripper::camera::kMainCameraRtcWriteAheadUs);
}

}  // namespace
