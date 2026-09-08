#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DA::Ugripper::detail {

inline constexpr size_t kTactileFrameBytes = 160U * 120U;
inline constexpr int kTactileFrameWidth = 160;
inline constexpr int kTactileFrameHeight = 120;
inline constexpr double kTactileRobustResidualAreaThreshold = 0.003;
inline constexpr double kTactileResidualFloorThreshold = 10.0;
inline constexpr double kTactileResidualMadMultiplier = 6.0;
inline constexpr double kTactileMadToSigma = 1.4826;
inline constexpr int kTactileResidualMinNeighborCount = 3;
inline constexpr size_t kTactileResidualMinComponentPixels = 8;
inline constexpr int kTactileResidualLowFrequencyBlurRadius = 2;
inline constexpr size_t kTactileResidualArtifactMaxComponentPixels = 25;
inline constexpr double kTactileResidualArtifactLowFrequencyMeanThreshold = 5.0;
inline constexpr int kTactileAlignmentSearchRadius = 2;
inline constexpr int kTactileAlignmentBlurRadius = 2;
inline constexpr int kTactileEdgeLocalToleranceRadius = 1;
inline constexpr double kTactileEdgeMagnitudeThreshold = 24.0;
inline constexpr double kTactileEdgeLowFrequencyThreshold = 8.0;

struct TactileFrameMetrics
{
    double robustResidualArea = 0.0;
    double rawResidualArea = 0.0;
    double residualThreshold = 0.0;
    double residualMedian = 0.0;
    double residualMad = 0.0;
    double residualMean = 0.0;
    double residualP99 = 0.0;
    double gain = 1.0;
    double offset = 0.0;
    int alignmentShiftX = 0;
    int alignmentShiftY = 0;
    double alignmentScore = 0.0;
    size_t edgeToleranceSuppressedPixels = 0;
    size_t maxResidualComponentPixels = 0;
    size_t droppedArtifactComponentPixels = 0;
    size_t droppedArtifactComponentCount = 0;
    bool damaged = false;
};

TactileFrameMetrics computeTactileFrameMetrics(const std::vector<uint8_t> &baseline,
                                                const std::vector<uint8_t> &current);
std::string formatTactileMetrics(const TactileFrameMetrics &metrics);

}  // namespace DA::Ugripper::detail
