#include "camera_recorder/camera_timestamp_offsets.h"
#include "fays_stereo_recorder/camera_timestamp_offsets.h"

#include <gtest/gtest.h>

namespace {

using camera_recorder::camera_time::CameraTimestampSource;

TEST(CameraTimestampOffsetsTest, KeepsAcceptedLatencyValuesInOnePlace)
{
    EXPECT_EQ(camera_recorder::camera_time::CameraLatencyUs(CameraTimestampSource::Main), 0);
    EXPECT_EQ(camera_recorder::camera_time::CameraLatencyUs(CameraTimestampSource::Tactile), 0);
    EXPECT_EQ(fays_stereo_recorder::camera_time::kStereoCameraLatencyUs, 0);
}

TEST(CameraTimestampOffsetsTest, AddsLatencyUsingRequestedUnit)
{
    EXPECT_EQ(
        camera_recorder::camera_time::AddCameraLatencyUs(1'000'000, CameraTimestampSource::Main),
        1'000'000);
    EXPECT_EQ(
        camera_recorder::camera_time::AddCameraLatencyUs(1'000'000, CameraTimestampSource::Tactile),
        1'000'000);
    EXPECT_EQ(
        fays_stereo_recorder::camera_time::AddStereoCameraLatencyNs(1'000'000'000ULL),
        1'000'000'000ULL);
}

}  // namespace
