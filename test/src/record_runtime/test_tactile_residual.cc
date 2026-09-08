#include "record_runtime/tactile_residual.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace DA::Ugripper::detail {
namespace {

std::vector<uint8_t> makeTexture()
{
    std::vector<uint8_t> frame(kTactileFrameBytes, 0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const int texture = ((x / 5 + y / 7) % 2 == 0) ? 28 : -18;
            const int fine = (x * 3 + y * 5) % 13;
            frame[static_cast<size_t>(y) * kTactileFrameWidth + static_cast<size_t>(x)] =
                static_cast<uint8_t>(std::clamp(115 + texture + fine, 0, 255));
        }
    }
    return frame;
}

std::vector<uint8_t> shiftWithEdge(const std::vector<uint8_t> &frame, int shiftY, int shiftX)
{
    std::vector<uint8_t> shifted(frame.size(), 0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const int sourceY = std::clamp(y - shiftY, 0, kTactileFrameHeight - 1);
            const int sourceX = std::clamp(x - shiftX, 0, kTactileFrameWidth - 1);
            shifted[static_cast<size_t>(y) * kTactileFrameWidth + static_cast<size_t>(x)] =
                frame[static_cast<size_t>(sourceY) * kTactileFrameWidth + static_cast<size_t>(sourceX)];
        }
    }
    return shifted;
}

TEST(TactileResidualTest, IdenticalFrameIsClean)
{
    const std::vector<uint8_t> frame = makeTexture();
    const TactileFrameMetrics metrics = computeTactileFrameMetrics(frame, frame);

    EXPECT_FALSE(metrics.damaged);
    EXPECT_DOUBLE_EQ(metrics.robustResidualArea, 0.0);
    EXPECT_EQ(metrics.alignmentShiftX, 0);
    EXPECT_EQ(metrics.alignmentShiftY, 0);
}

TEST(TactileResidualTest, GlobalOneAndTwoPixelTranslationsAreAligned)
{
    const std::vector<uint8_t> baseline = makeTexture();
    for (const auto [shiftY, shiftX] : {std::pair{0, 1}, std::pair{0, 2}, std::pair{1, 0},
                                            std::pair{2, 0}, std::pair{1, 1}, std::pair{-2, 2}})
    {
        const TactileFrameMetrics metrics =
            computeTactileFrameMetrics(baseline, shiftWithEdge(baseline, shiftY, shiftX));
        EXPECT_FALSE(metrics.damaged) << "input shift dy=" << shiftY << " dx=" << shiftX;
        EXPECT_EQ(metrics.alignmentShiftX, -shiftX);
        EXPECT_EQ(metrics.alignmentShiftY, -shiftY);
    }
}

TEST(TactileResidualTest, OnePixelLongTextureEdgeDriftIsSuppressed)
{
    std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    for (int y = 12; y < kTactileFrameHeight - 12; ++y)
    {
        const size_t baselineIndex = static_cast<size_t>(y) * kTactileFrameWidth + 78;
        const size_t currentIndex = static_cast<size_t>(y) * kTactileFrameWidth + 79;
        baseline[baselineIndex] = static_cast<uint8_t>(std::min(255, static_cast<int>(baseline[baselineIndex]) + 22));
        current[currentIndex] = static_cast<uint8_t>(std::min(255, static_cast<int>(current[currentIndex]) + 22));
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
    EXPECT_FALSE(metrics.damaged);
    EXPECT_GT(metrics.edgeToleranceSuppressedPixels, 0U);
    EXPECT_DOUBLE_EQ(metrics.robustResidualArea, 0.0);
}

TEST(TactileResidualTest, SolidDamageBlockRemainsDetected)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    for (int y = 45; y < 61; ++y)
    {
        for (int x = 68; x < 84; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kTactileFrameWidth + static_cast<size_t>(x);
            current[index] = static_cast<uint8_t>(std::min(255, static_cast<int>(current[index]) + 70));
        }
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
    EXPECT_TRUE(metrics.damaged);
    EXPECT_GE(metrics.robustResidualArea, kTactileRobustResidualAreaThreshold);
}

TEST(TactileResidualTest, InvalidFrameSizeFailsClosed)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    current.pop_back();

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
    EXPECT_TRUE(metrics.damaged);
    EXPECT_DOUBLE_EQ(metrics.robustResidualArea, 1.0);
}

}  // namespace
}  // namespace DA::Ugripper::detail
