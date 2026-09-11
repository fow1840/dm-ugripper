#include "record_runtime/tactile_residual.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
            frame[static_cast<size_t>(y) * kTactileFrameWidth + static_cast<size_t>(x)] =
                static_cast<uint8_t>(60 + (x * 3 + y * 5) % 121);
        }
    }
    return frame;
}

std::vector<uint8_t> applyNonlinearBrightness(const std::vector<uint8_t> &frame)
{
    std::vector<uint8_t> adjusted = frame;
    for (uint8_t &value : adjusted)
    {
        const double normalized = static_cast<double>(value) / 255.0;
        value = static_cast<uint8_t>(std::lround(std::pow(normalized, 0.78) * 255.0));
    }
    return adjusted;
}

TEST(TactileResidualTest, IdenticalFrameIsClean)
{
    const std::vector<uint8_t> frame = makeTexture();
    const TactileFrameMetrics metrics = computeTactileFrameMetrics(frame, frame);

    EXPECT_FALSE(metrics.damaged);
    EXPECT_DOUBLE_EQ(metrics.robustResidualArea, 0.0);
    EXPECT_EQ(metrics.solidCorePixels, 0U);
    EXPECT_EQ(metrics.alignmentShiftX, 0);
    EXPECT_EQ(metrics.alignmentShiftY, 0);
}

TEST(TactileResidualTest, OnePixelLongTextureEdgeDriftHasNoSolidCore)
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
    EXPECT_EQ(metrics.edgeToleranceSuppressedPixels, 0U);
    EXPECT_EQ(metrics.solidCorePixels, 0U);
    EXPECT_DOUBLE_EQ(metrics.robustResidualArea, 0.0);
}

TEST(TactileResidualTest, TwoPixelLinesAndBorderLineHaveNoSolidCore)
{
    const std::vector<uint8_t> baseline = makeTexture();
    for (const bool atBorder : {false, true})
    {
        std::vector<uint8_t> current = baseline;
        const int startX = atBorder ? kTactileFrameWidth - 2 : 78;
        for (int y = 8; y < kTactileFrameHeight - 8; ++y)
        {
            for (int x = startX; x < startX + 2; ++x)
            {
                const size_t index = static_cast<size_t>(y) * kTactileFrameWidth +
                                     static_cast<size_t>(x);
                current[index] = static_cast<uint8_t>(
                    std::min(255, static_cast<int>(current[index]) + 70));
            }
        }

        const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
        EXPECT_FALSE(metrics.damaged) << "atBorder=" << atBorder;
        EXPECT_EQ(metrics.solidCorePixels, 0U);
        EXPECT_GT(metrics.droppedCurvePixels, 0U);
    }
}

TEST(TactileResidualTest, TwoPixelCurvedLineHasNoSolidCore)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    for (int x = 18; x < kTactileFrameWidth - 18; ++x)
    {
        const int centerY = 58 + ((x / 8) % 2);
        for (int y = centerY; y < centerY + 2; ++y)
        {
            const size_t index = static_cast<size_t>(y) * kTactileFrameWidth +
                                 static_cast<size_t>(x);
            current[index] = static_cast<uint8_t>(
                std::min(255, static_cast<int>(current[index]) + 70));
        }
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
    EXPECT_FALSE(metrics.damaged);
    EXPECT_EQ(metrics.solidCorePixels, 0U);
    EXPECT_GT(metrics.droppedCurvePixels, 0U);
}

TEST(TactileResidualTest, FourByFourBlockIsBelowSolidCorePixelThreshold)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    for (int y = 54; y < 58; ++y)
    {
        for (int x = 78; x < 82; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kTactileFrameWidth +
                                 static_cast<size_t>(x);
            current[index] = static_cast<uint8_t>(
                std::min(255, static_cast<int>(current[index]) + 70));
        }
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
    EXPECT_FALSE(metrics.damaged);
    EXPECT_LT(metrics.solidCorePixels, kTactileResidualMinSolidCorePixels);
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
    EXPECT_GE(metrics.solidCorePixels, kTactileResidualMinSolidCorePixels);
}

TEST(TactileResidualTest, ConditionalToneLutRemovesNonlinearBrightnessChange)
{
    const std::vector<uint8_t> baseline = makeTexture();
    const std::vector<uint8_t> current = applyNonlinearBrightness(baseline);

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);

    EXPECT_TRUE(metrics.toneLutApplied);
    EXPECT_FALSE(metrics.damaged);
    EXPECT_EQ(metrics.solidCorePixels, 0U);
    EXPECT_LT(metrics.toneLutResidualP99,
              metrics.affineResidualP99 * kTactileToneLutMaxP99Ratio);
}

TEST(TactileResidualTest, SolidDamageSurvivesNonlinearBrightnessCorrection)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = applyNonlinearBrightness(baseline);
    for (int y = 45; y < 61; ++y)
    {
        for (int x = 68; x < 84; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kTactileFrameWidth +
                                 static_cast<size_t>(x);
            current[index] = static_cast<uint8_t>(
                std::min(255, static_cast<int>(current[index]) + 70));
        }
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);

    EXPECT_TRUE(metrics.damaged);
    EXPECT_GE(metrics.solidCorePixels, kTactileResidualMinSolidCorePixels);
}

TEST(TactileResidualTest, DifferentTextureIsReportedAsBaselineMismatch)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current(kTactileFrameBytes, 0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            current[static_cast<size_t>(y) * kTactileFrameWidth + static_cast<size_t>(x)] =
                static_cast<uint8_t>(60 + (x * 11 + y * 17) % 121);
        }
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);

    EXPECT_FALSE(metrics.baselineCompatible);
    EXPECT_TRUE(metrics.baselineRefreshRequired);
    EXPECT_LT(metrics.baselineCorrelation, kTactileBaselineMinCorrelation);
    EXPECT_FALSE(metrics.damaged);
    EXPECT_EQ(metrics.solidCorePixels, 0U);
    EXPECT_FALSE(metrics.toneLutApplied);
}

TEST(TactileResidualTest, NinetyPercentLocalizedStructuralDamageIsNotBaselineMismatch)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    const int damagedRows = kTactileFrameHeight * 9 / 10;
    for (int y = 0; y < damagedRows; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kTactileFrameWidth +
                                 static_cast<size_t>(x);
            current[index] = static_cast<uint8_t>(255 - current[index]);
        }
    }

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);

    EXPECT_TRUE(metrics.baselineCompatible);
    EXPECT_FALSE(metrics.baselineRefreshRequired);
    EXPECT_TRUE(metrics.largeStructuralDamage);
    EXPECT_TRUE(metrics.damaged);
}

TEST(TactileResidualTest, InvalidFrameSizeFailsClosed)
{
    const std::vector<uint8_t> baseline = makeTexture();
    std::vector<uint8_t> current = baseline;
    current.pop_back();

    const TactileFrameMetrics metrics = computeTactileFrameMetrics(baseline, current);
    EXPECT_TRUE(metrics.damaged);
    EXPECT_DOUBLE_EQ(metrics.robustResidualArea, 1.0);
    EXPECT_EQ(metrics.solidCorePixels, kTactileFrameBytes);
}

TEST(TactileResidualTest, ExpensiveSpatialToleranceIsDisabledByDefault)
{
    EXPECT_EQ(kTactileAlignmentSearchRadius, 0);
    EXPECT_EQ(kTactileEdgeLocalToleranceRadius, 0);
}

}  // namespace
}  // namespace DA::Ugripper::detail
