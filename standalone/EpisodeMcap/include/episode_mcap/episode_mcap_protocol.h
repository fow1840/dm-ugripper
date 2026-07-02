#pragma once

#include <cstdint>

namespace ugripper::episode_mcap {

constexpr uint32_t kFrameMagic = 0x50434d55U;  // "UMCP" little-endian.
constexpr uint16_t kFrameVersion = 1;

enum class FrameKind : uint16_t {
    CompressedImageH264 = 1,
    CameraInfo = 2,
    Json = 3,
    EncoderBinary = 4,
    FaysImu = 5,
    CompressedImageH265 = 6,
};

struct FrameHeader {
    uint32_t magic = kFrameMagic;
    uint16_t version = kFrameVersion;
    uint16_t kind = 0;
    uint32_t topic_size = 0;
    uint32_t frame_id_size = 0;
    uint64_t log_time_ns = 0;
    uint64_t publish_time_ns = 0;
    uint32_t sequence = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t payload_size = 0;
};

static_assert(sizeof(FrameHeader) == 48, "Episode MCAP frame header layout changed");

}  // namespace ugripper::episode_mcap
