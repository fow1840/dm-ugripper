#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ugripper::episode_mcap {

struct RosTime {
    int32_t sec = 0;
    uint32_t nanosec = 0;
};

RosTime RosTimeFromUnixNs(uint64_t unix_time_ns);

std::vector<std::byte> EncodeCompressedImage(uint64_t stamp_ns,
                                             const std::string& frame_id,
                                             const std::string& format,
                                             const uint8_t* data,
                                             size_t data_size);

std::vector<std::byte> EncodeCameraInfo(uint64_t stamp_ns,
                                        const std::string& frame_id,
                                        uint32_t width,
                                        uint32_t height,
                                        const std::string& distortion_model = "",
                                        const std::vector<double>& d = {},
                                        const std::vector<double>& k = {},
                                        const std::vector<double>& r = {},
                                        const std::vector<double>& p = {});

std::vector<std::byte> EncodeImu(uint64_t stamp_ns,
                                 const std::string& frame_id,
                                 double gx,
                                 double gy,
                                 double gz,
                                 double ax,
                                 double ay,
                                 double az);

const char* CompressedImageSchema();
const char* CameraInfoSchema();
const char* ImuSchema();

}  // namespace ugripper::episode_mcap
