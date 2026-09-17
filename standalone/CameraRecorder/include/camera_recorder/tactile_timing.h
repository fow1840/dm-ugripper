#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ugripper::camera {
// FFmpeg's input normalization offset is printed as an exact integer in us.
// Do not parse off_time/pkt_pts_time: their floating-point display loses precision
// for Unix timestamps. Only the input normalization line establishes the anchor.
inline std::optional<int64_t> TactileUnixOriginFromFfmpeg(std::string_view line) {
    if (line.find("demuxer+ffmpeg ->") == std::string_view::npos) return std::nullopt;
    const auto pos = line.find(" off:");
    if (pos == std::string_view::npos) return std::nullopt;
    const auto start = pos + 5;
    const auto end = line.find(' ', start);
    const auto token = line.substr(start, end == std::string_view::npos ? end : end-start);
    int64_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data()+token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data()+token.size()) return std::nullopt;
    // Reject monotonic timestamps and overflow before negating.
    if (value > -946684800000000LL || value < -4102444800000000LL) return std::nullopt;
    return -value;
}
}
