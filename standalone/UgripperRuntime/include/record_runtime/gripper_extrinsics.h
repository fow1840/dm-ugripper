#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>

namespace DA::Ugripper {

inline nlohmann::json DefaultMainCameraExtrinsics()
{
    return {
        {"T_base_main_camera", {
            {-3.67320510334657e-06, -0.990267559376913, -0.139176725181332, 0.078813},
            {0.999999999993254, -3.63745585280637e-06, -5.11224657206582e-07, 0.0},
            {0.0, -0.139176725182271, 0.990267559383593, -0.025092},
            {0.0, 0.0, 0.0, 1.0}}},
        {"T_main_camera_stereo_cam0", {
            {1.0, 0.0, 0.0, -0.0399999137418547},
            {0.0, 0.788007746557592, -0.615665324153656, 0.0234206364282246},
            {0.0, 0.615665324153656, 0.788007746557592, 0.00208691828807489},
            {0.0, 0.0, 0.0, 1.0}}}
    };
}

inline bool ReadGripperTransform(const std::array<uint8_t, 1024> &payload,
                                 uint32_t minimumVersion, unsigned validBit,
                                 size_t offset, nlohmann::json *matrix)
{
    const auto u16 = [&](size_t at) -> uint16_t {
        return uint16_t(payload[at]) | (uint16_t(payload[at + 1]) << 8);
    };
    const auto u32 = [&](size_t at) -> uint32_t {
        return uint32_t(payload[at]) | (uint32_t(payload[at + 1]) << 8) |
               (uint32_t(payload[at + 2]) << 16) | (uint32_t(payload[at + 3]) << 24);
    };
    const uint32_t version = u32(4);
    if (matrix == nullptr || offset > payload.size() - 64 || validBit >= 32 ||
        std::memcmp(payload.data(), "UCAL", 4) != 0 ||
        version < minimumVersion || version > 0x00030000u ||
        (version != 0x00010000u && version != 0x00020000u && version != 0x00030000u) ||
        u16(10) != 32 || u16(8) > payload.size() || u16(8) < offset + 64 ||
        (u32(12) & (uint32_t(1) << validBit)) == 0)
        return false;

    std::array<float, 16> values{};
    for (size_t i = 0; i < values.size(); ++i)
    {
        const uint32_t bits = u32(offset + i * 4);
        std::memcpy(&values[i], &bits, sizeof(float));
        if (!std::isfinite(values[i])) return false;
    }
    if (std::abs(values[12]) > 1e-5f || std::abs(values[13]) > 1e-5f ||
        std::abs(values[14]) > 1e-5f || std::abs(values[15] - 1.0f) > 1e-5f)
        return false;
    // Reject corrupt rotations while retaining the stored values verbatim.
    for (size_t row = 0; row < 3; ++row)
        for (size_t other = row; other < 3; ++other)
        {
            double dot = 0;
            for (size_t col = 0; col < 3; ++col)
                dot += double(values[row * 4 + col]) * values[other * 4 + col];
            if (std::abs(dot - (row == other ? 1.0 : 0.0)) > 0.02) return false;
        }
    const double determinant =
        values[0] * (double(values[5]) * values[10] - double(values[6]) * values[9]) -
        values[1] * (double(values[4]) * values[10] - double(values[6]) * values[8]) +
        values[2] * (double(values[4]) * values[9] - double(values[5]) * values[8]);
    if (std::abs(determinant - 1.0) > 0.02) return false;
    *matrix = nlohmann::json::array();
    for (size_t row = 0; row < 4; ++row)
        matrix->push_back({values[row * 4], values[row * 4 + 1],
                           values[row * 4 + 2], values[row * 4 + 3]});
    return true;
}

}  // namespace DA::Ugripper
