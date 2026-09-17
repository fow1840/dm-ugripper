#include "camera_recorder/camera_timestamp_offsets.h"
#include "fays_stereo_recorder/camera_timestamp_offsets.h"

#include <gtest/gtest.h>

namespace {

using camera_recorder::camera_time::CameraTimestampSource;

TEST(CameraTimestampOffsetsTest, KeepsAcceptedTimestampOffsetsInOnePlace)
{
    EXPECT_EQ(camera_recorder::camera_time::CameraLatencyUs(CameraTimestampSource::Main), -28'000);
    EXPECT_EQ(camera_recorder::camera_time::CameraLatencyUs(CameraTimestampSource::Tactile), -2'700);
    EXPECT_EQ(fays_stereo_recorder::camera_time::kStereoCameraLatencyUs, -1'000);
}

TEST(CameraTimestampOffsetsTest, AppliesSignedOffsetsUsingRequestedUnit)
{
    EXPECT_EQ(
        camera_recorder::camera_time::AddCameraLatencyUs(1'000'000, CameraTimestampSource::Main),
        972'000);
    EXPECT_EQ(
        camera_recorder::camera_time::AddCameraLatencyUs(1'000'000, CameraTimestampSource::Tactile),
        997'300);
    EXPECT_EQ(
        fays_stereo_recorder::camera_time::AddStereoCameraLatencyNs(1'000'000'000ULL),
        999'000'000ULL);
    EXPECT_EQ(
        fays_stereo_recorder::camera_time::AddStereoCameraLatencyNs(500'000ULL),
        0ULL);
}

TEST(CameraTimestampOffsetsTest, CorrectsFirstTenFrameMeanWithoutChangingRelativeIntervals)
{
    struct Sample { CameraTimestampSource source; const char* name; int64_t oldOffset, error; };
    const Sample samples[] = {
        {CameraTimestampSource::Main, "left_cam_main", -28000, -800},
        {CameraTimestampSource::Main, "right_cam_main", -28000, 1200},
        {CameraTimestampSource::Tactile, "left_tcam_l", -2700, -1200},
        {CameraTimestampSource::Tactile, "left_tcam_r", -2700, -600},
        {CameraTimestampSource::Tactile, "right_tcam_l", -2700, -600},
        {CameraTimestampSource::Tactile, "right_tcam_r", -2700, -900},
    };
    for (const auto& s : samples) {
        const auto offset = camera_recorder::camera_time::CameraLatencyUs(s.source, s.name);
        EXPECT_EQ(s.error + offset - s.oldOffset, 0) << s.name;
        const auto first = camera_recorder::camera_time::AddCameraLatencyUs(1000000, s.source, s.name);
        const auto next = camera_recorder::camera_time::AddCameraLatencyUs(1012000, s.source, s.name);
        EXPECT_EQ(next - first, 12000) << s.name;
    }
    EXPECT_EQ(fays_stereo_recorder::camera_time::AddStereoCameraLatencyNs(1000000000, "stereo_left.mkv"), 999000000);
    EXPECT_EQ(fays_stereo_recorder::camera_time::AddStereoCameraLatencyNs(1000000000, "stereo_right.mkv"), 1000000000);
}

}  // namespace
