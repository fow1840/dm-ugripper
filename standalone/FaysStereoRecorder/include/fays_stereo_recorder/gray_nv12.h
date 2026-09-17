#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fays_stereo_recorder {

// Packed GRAY8 (full range) -> packed NV12 (limited range). The SDK's gray
// image is already luminance; neutral chroma avoids a GRAY -> BGR -> YUV trip.
inline bool GrayToNv12(const uint8_t* gray, size_t grayBytes, int width, int height,
                       std::vector<uint8_t>& nv12) {
    if (!gray || width <= 0 || height <= 0 || width % 2 || height % 2) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h) return false;
    const size_t pixels = w * h;
    if (grayBytes < pixels || pixels > std::numeric_limits<size_t>::max() - pixels / 2) {
        return false;
    }
    nv12.resize(pixels + pixels / 2);
    for (size_t i = 0; i < pixels; ++i) {
        nv12[i] = static_cast<uint8_t>(16 + (219u * gray[i] + 127u) / 255u);
    }
    // Refresh chroma even when reusing storage after a resolution change.
    std::fill(nv12.begin() + pixels, nv12.end(), uint8_t{128});
    return true;
}

}  // namespace fays_stereo_recorder
