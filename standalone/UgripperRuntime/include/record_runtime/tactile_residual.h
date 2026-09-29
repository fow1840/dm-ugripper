#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DA::Ugripper::detail {

inline constexpr size_t kTactileFrameBytes = 160U * 120U;
inline constexpr int kTactileFrameWidth = 160;
inline constexpr int kTactileFrameHeight = 120;
inline constexpr double kTactileResidualFloorThreshold = 10.0;
inline constexpr double kTactileResidualMadMultiplier = 6.0;
inline constexpr double kTactileMadToSigma = 1.4826;
inline constexpr int kTactileResidualMinNeighborCount = 3;
inline constexpr size_t kTactileResidualMinComponentPixels = 8;
inline constexpr size_t kTactileResidualMinSolidCorePixels = 8;
inline constexpr int kTactileAlignmentSearchRadius = 0;
inline constexpr int kTactileAlignmentBlurRadius = 2;
inline constexpr int kTactileEdgeLocalToleranceRadius = 0;
inline constexpr double kTactileEdgeMagnitudeThreshold = 24.0;
inline constexpr double kTactileEdgeLowFrequencyThreshold = 8.0;
inline constexpr double kTactileToneLutMinMeanShift = 8.0;
inline constexpr double kTactileToneLutMaxResidualP99 = 8.5;
inline constexpr double kTactileToneLutMaxP99Ratio = 0.60;
inline constexpr double kTactileBaselineMinCorrelation = 0.90;
inline constexpr int kTactileBaselineBlockColumns = 10;
inline constexpr int kTactileBaselineBlockRows = 16;
inline constexpr double kTactileBaselineBlockMinCorrelation = 0.85;
inline constexpr double kTactileBaselineMismatchMaxMatchingBlockRatio = 0.05;

struct TactileFrameMetrics
{
    double robustResidualArea = 0.0;
    double rawResidualArea = 0.0;
    double residualThreshold = 0.0;
    double residualMedian = 0.0;
    double residualMad = 0.0;
    double residualMean = 0.0;
    double residualP99 = 0.0;
    double affineResidualP99 = 0.0;
    double toneLutResidualP99 = 0.0;
    double toneLutCorrelationGain = 0.0;
    double baselineCorrelation = 1.0;
    double baselineMatchingBlockRatio = 1.0;
    double gain = 1.0;
    double offset = 0.0;
    int alignmentShiftX = 0;
    int alignmentShiftY = 0;
    double alignmentScore = 0.0;
    size_t edgeToleranceSuppressedPixels = 0;
    size_t maxResidualComponentPixels = 0;
    size_t maxSolidCoreComponentPixels = 0;
    size_t solidCorePixels = 0;
    size_t droppedCurvePixels = 0;
    size_t droppedCurveComponentCount = 0;
    bool toneLutApplied = false;
    bool toneLutAttempted = false;
    bool baselineCompatible = true;
    bool baselineRefreshRequired = false;
    bool largeStructuralDamage = false;
    bool damaged = false;
};

TactileFrameMetrics computeTactileFrameMetrics(const std::vector<uint8_t> &baseline,
                                                const std::vector<uint8_t> &current,
                                                std::vector<uint8_t> *mask = nullptr);
std::string formatTactileMetrics(const TactileFrameMetrics &metrics);

}  // namespace DA::Ugripper::detail
