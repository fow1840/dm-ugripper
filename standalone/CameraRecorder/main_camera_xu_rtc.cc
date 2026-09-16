#include "camera_recorder/main_camera_xu_rtc.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <linux/videodev2.h>
#include <sys/ioctl.h>

#if defined(__has_include)
#if __has_include(<linux/uvcvideo.h>)
#include <linux/uvcvideo.h>
#endif
#endif

#ifndef UVCIOC_CTRL_QUERY
#define UVCIOC_CTRL_QUERY _IOWR('u', 0x21, struct uvc_xu_control_query)
struct uvc_xu_control_query {
    uint8_t unit;
    uint8_t selector;
    uint8_t query;
    uint16_t size;
    uint8_t* data;
};
#endif

#ifndef UVC_SET_CUR
#define UVC_SET_CUR 0x01
#endif

#ifndef UVC_GET_CUR
#define UVC_GET_CUR 0x81
#endif

namespace ugripper::camera {
namespace {

constexpr uint8_t kPreferredXuRtcUnit = 3;
constexpr uint8_t kFallbackXuRtcUnit = 2;
constexpr uint8_t kXuRtcSelector = 0x19;
constexpr uint16_t kXuRtcSize = 8;
constexpr uint8_t kUserDataUnregisteredPayloadType = 5;
constexpr size_t kXuTimestampPayloadSize = 36;

constexpr uint8_t kXuTimestampUuid[16] = {
    0x8f, 0xe5, 0x4d, 0x3a, 0x21, 0x7c, 0x4b, 0x6f,
    0x93, 0x2d, 0x50, 0x54, 0x53, 0x48, 0x32, 0x35,
};

bool XuQueryUnit(int fd, uint8_t unit, uint8_t query, uint8_t data[kXuRtcSize]) {
    uvc_xu_control_query control{};
    control.unit = unit;
    control.selector = kXuRtcSelector;
    control.query = query;
    control.size = kXuRtcSize;
    control.data = data;
    return ioctl(fd, UVCIOC_CTRL_QUERY, &control) == 0;
}

bool XuQuery(int fd, uint8_t query, uint8_t data[kXuRtcSize],
             std::string* error_message) {
    if (XuQueryUnit(fd, kPreferredXuRtcUnit, query, data)) {
        return true;
    }
    const int preferred_errno = errno;
    if (preferred_errno != ENOENT) {
        if (error_message != nullptr) {
            *error_message = "UVCIOC_CTRL_QUERY failed: unit=3 error=" +
                             std::string(std::strerror(preferred_errno)) +
                             " selector=0x19 query=" + std::to_string(query);
        }
        return false;
    }
    if (XuQueryUnit(fd, kFallbackXuRtcUnit, query, data)) {
        return true;
    }
    const int fallback_errno = errno;
    if (error_message != nullptr) {
        *error_message = "UVCIOC_CTRL_QUERY failed: unit=3 error=" +
                         std::string(std::strerror(preferred_errno)) +
                         ", unit=2 error=" + std::string(std::strerror(fallback_errno)) +
                         " selector=0x19 query=" + std::to_string(query);
    }
    return false;
}

uint64_t LoadLittleEndian64(const uint8_t* data) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(data[index]) << (index * 8);
    }
    return value;
}

uint64_t LoadBigEndian64(const uint8_t* data) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value = (value << 8) | data[index];
    }
    return value;
}

std::optional<std::pair<size_t, size_t>> FindStartCode(
    const uint8_t* data, size_t size, size_t from) {
    if (data == nullptr || from >= size) {
        return std::nullopt;
    }
    for (size_t index = from; index + 3 <= size; ++index) {
        if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
            return std::make_pair(index, size_t{3});
        }
        if (index + 4 <= size && data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 0 && data[index + 3] == 1) {
            return std::make_pair(index, size_t{4});
        }
    }
    return std::nullopt;
}

std::optional<MainCameraXuTimestamp> ParseSeiNal(
    const uint8_t* nal, size_t nal_size, MainCameraCodec codec) {
    const size_t header_size = codec == MainCameraCodec::H264 ? 1 : 2;
    if (nal == nullptr || nal_size <= header_size) {
        return std::nullopt;
    }
    const uint8_t nal_type = codec == MainCameraCodec::H264
        ? nal[0] & 0x1f : (nal[0] >> 1) & 0x3f;
    if (codec == MainCameraCodec::H264 ? nal_type != 6 : (nal_type != 39 && nal_type != 40)) {
        return std::nullopt;
    }

    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal_size - header_size);
    unsigned int zero_count = 0;
    for (size_t index = header_size; index < nal_size; ++index) {
        const uint8_t value = nal[index];
        if (zero_count >= 2 && value == 0x03) {
            zero_count = 0;
            continue;
        }
        rbsp.push_back(value);
        if (value == 0) {
            zero_count = std::min(zero_count + 1, 2U);
        } else {
            zero_count = 0;
        }
    }

    size_t position = 0;
    while (position + 1 < rbsp.size()) {
        unsigned int payload_type = 0;
        while (position < rbsp.size() && rbsp[position] == 0xff) {
            payload_type += 255;
            ++position;
        }
        if (position >= rbsp.size()) {
            return std::nullopt;
        }
        payload_type += rbsp[position++];

        size_t payload_size = 0;
        while (position < rbsp.size() && rbsp[position] == 0xff) {
            payload_size += 255;
            ++position;
        }
        if (position >= rbsp.size()) {
            return std::nullopt;
        }
        payload_size += rbsp[position++];
        if (payload_size > rbsp.size() - position) {
            return std::nullopt;
        }

        const uint8_t* payload = rbsp.data() + position;
        if (payload_type == kUserDataUnregisteredPayloadType &&
            payload_size >= kXuTimestampPayloadSize &&
            std::memcmp(payload, kXuTimestampUuid, sizeof(kXuTimestampUuid)) == 0 &&
            payload[32] == 'X' && payload[33] == 'U' &&
            payload[34] == '2' && payload[35] == 0) {
            MainCameraXuTimestamp result;
            result.encoder_pts_us = LoadBigEndian64(payload + 16);
            result.xu_time_us = LoadBigEndian64(payload + 24);
            return result;
        }
        position += payload_size;
    }
    return std::nullopt;
}

std::optional<MainCameraXuTimestamp> ParseAnnexB(
    const uint8_t* data, size_t size, MainCameraCodec codec) {
    auto current = FindStartCode(data, size, 0);
    while (current.has_value()) {
        const size_t nal_begin = current->first + current->second;
        const auto next = FindStartCode(data, size, nal_begin);
        const size_t nal_end = next.has_value() ? next->first : size;
        if (nal_end > nal_begin) {
            if (auto result = ParseSeiNal(data + nal_begin, nal_end - nal_begin, codec);
                result.has_value()) {
                return result;
            }
        }
        current = next;
    }
    return std::nullopt;
}

std::optional<MainCameraXuTimestamp> ParseLengthPrefixed(
    const uint8_t* data, size_t size, MainCameraCodec codec) {
    size_t offset = 0;
    while (offset + 4 <= size) {
        const uint32_t nal_size =
            (static_cast<uint32_t>(data[offset]) << 24) |
            (static_cast<uint32_t>(data[offset + 1]) << 16) |
            (static_cast<uint32_t>(data[offset + 2]) << 8) |
            static_cast<uint32_t>(data[offset + 3]);
        offset += 4;
        if (nal_size == 0 || nal_size > size - offset) {
            return std::nullopt;
        }
        if (auto result = ParseSeiNal(data + offset, nal_size, codec); result.has_value()) {
            return result;
        }
        offset += nal_size;
    }
    return std::nullopt;
}

}  // namespace

std::vector<uint8_t> NormalizeMainCameraH264AccessUnit(const uint8_t* data, size_t size) {
    if (!data || !size) return {};
    std::vector<std::pair<size_t, size_t>> trailing_sei;
    std::optional<size_t> first_vcl;
    auto current = FindStartCode(data, size, 0);
    while (current) {
        const size_t begin = current->first + current->second;
        const auto next = FindStartCode(data, size, begin);
        const size_t end = next ? next->first : size;
        if (begin < end) {
            const auto type = data[begin] & 0x1f;
            if (type >= 1 && type <= 5 && !first_vcl) first_vcl = current->first;
            if (type == 6 && first_vcl) trailing_sei.emplace_back(current->first, end);
        }
        current = next;
    }
    if (trailing_sei.empty()) return {data, data + size};
    std::vector<uint8_t> result;
    result.reserve(size);
    result.insert(result.end(), data, data + *first_vcl);
    for (const auto& range : trailing_sei)
        result.insert(result.end(), data + range.first, data + range.second);
    size_t cursor = *first_vcl;
    for (const auto& range : trailing_sei) {
        result.insert(result.end(), data + cursor, data + range.first);
        cursor = range.second;
    }
    result.insert(result.end(), data + cursor, data + size);
    return result;
}

bool SetMainCameraRtcUs(int fd, uint64_t timestamp_us, std::string* error_message) {
    if (timestamp_us == 0) {
        if (error_message != nullptr) {
            *error_message = "main camera RTC timestamp must be non-zero";
        }
        return false;
    }
    uint8_t data[kXuRtcSize]{};
    for (size_t index = 0; index < kXuRtcSize; ++index) {
        data[index] = static_cast<uint8_t>(timestamp_us >> (index * 8));
    }
    return XuQuery(fd, UVC_SET_CUR, data, error_message);
}

std::optional<uint64_t> GetMainCameraRtcPsetUs(int fd, std::string* error_message) {
    uint8_t data[kXuRtcSize]{};
    if (!XuQuery(fd, UVC_GET_CUR, data, error_message)) {
        return std::nullopt;
    }
    return LoadLittleEndian64(data);
}

std::optional<MainCameraXuTimestamp> ParseMainCameraXuTimestamp(
    const uint8_t* access_unit, size_t size, MainCameraCodec codec) {
    if (access_unit == nullptr || size < 4) {
        return std::nullopt;
    }
    if (FindStartCode(access_unit, size, 0).has_value()) {
        return ParseAnnexB(access_unit, size, codec);
    }
    return ParseLengthPrefixed(access_unit, size, codec);
}

std::optional<int64_t> MainCameraFrameUnixTimeUs(
    const MainCameraXuTimestamp& timestamp,
    uint64_t encoder_pts_at_set_us) {
    constexpr uint64_t kMax = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (timestamp.xu_time_us == 0 || timestamp.xu_time_us > kMax) {
        return std::nullopt;
    }
    // SEI can carry the new SET value for a frame captured before that SET.
    // Subtract unsigned magnitudes separately to avoid underflow and signed overflow.
    if (timestamp.encoder_pts_us < encoder_pts_at_set_us) {
        const uint64_t before_set_us = encoder_pts_at_set_us - timestamp.encoder_pts_us;
        if (before_set_us >= timestamp.xu_time_us) {
            return std::nullopt;
        }
        return static_cast<int64_t>(timestamp.xu_time_us - before_set_us);
    }
    const uint64_t elapsed_since_set_us = timestamp.encoder_pts_us - encoder_pts_at_set_us;
    if (elapsed_since_set_us > kMax - timestamp.xu_time_us) {
        return std::nullopt;
    }
    return static_cast<int64_t>(timestamp.xu_time_us + elapsed_since_set_us);
}

std::optional<int64_t> MainCameraRtcWriteTimeUs(int64_t host_unix_time_us) {
    if (host_unix_time_us <= 0 ||
        (kMainCameraRtcWriteAheadUs > 0 &&
         host_unix_time_us > std::numeric_limits<int64_t>::max() - kMainCameraRtcWriteAheadUs)) {
        return std::nullopt;
    }
    const int64_t adjusted = host_unix_time_us + kMainCameraRtcWriteAheadUs;
    return adjusted > 0 ? std::optional<int64_t>(adjusted) : std::nullopt;
}

}  // namespace ugripper::camera
