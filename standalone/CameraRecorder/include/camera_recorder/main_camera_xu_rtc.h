#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ugripper::camera {

// Unified write-ahead measured on the RK3588 target from the software-visible
// UVC SET ioctl interval. Left/right device-specific midpoint estimates were
// about 350/100 us; 200 us minimizes the worst median error across both.
inline constexpr int64_t kMainCameraRtcWriteAheadUs = 200;

struct MainCameraXuTimestamp {
    uint64_t encoder_pts_us = 0;
    uint64_t xu_time_us = 0;
};

enum class MainCameraCodec { H264, H265 };

// Firmware appends H.264 SEI after VCL. Move SEI before the first VCL in this
// Annex B access unit so parsers keep its timestamp with the intended picture.
std::vector<uint8_t> NormalizeMainCameraH264AccessUnit(const uint8_t* data, size_t size);

bool SetMainCameraRtcUs(int fd, uint64_t timestamp_us, std::string* error_message = nullptr);

// The updated firmware latches the encoder PTS at the accepted XU SET. GET
// returns that Pset value, not the Unix timestamp previously written by SET.
std::optional<uint64_t> GetMainCameraRtcPsetUs(
    int fd, std::string* error_message = nullptr);

std::optional<MainCameraXuTimestamp> ParseMainCameraXuTimestamp(
    const uint8_t* access_unit, size_t size,
    MainCameraCodec codec = MainCameraCodec::H265);

// Frame PTS can precede Pset at an in-stream SET transition. Apply the signed
// difference, rejecting zero/negative Unix results and int64 overflow.
std::optional<int64_t> MainCameraFrameUnixTimeUs(
    const MainCameraXuTimestamp& timestamp,
    uint64_t encoder_pts_at_set_us);

std::optional<int64_t> MainCameraRtcWriteTimeUs(int64_t host_unix_time_us);

}  // namespace ugripper::camera
