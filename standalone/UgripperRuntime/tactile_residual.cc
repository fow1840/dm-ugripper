#include "record_runtime/tactile_residual.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace DA::Ugripper::detail {
namespace {

constexpr size_t kWidth = static_cast<size_t>(kTactileFrameWidth);

std::string formatFixed(double value, int precision)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

double percentileFromSorted(const std::vector<double> &sortedValues, double percentile)
{
    if (sortedValues.empty())
    {
        return 0.0;
    }
    if (sortedValues.size() == 1)
    {
        return sortedValues.front();
    }

    const double clamped = std::clamp(percentile, 0.0, 1.0);
    const double position = clamped * static_cast<double>(sortedValues.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = std::min(lower + 1, sortedValues.size() - 1);
    const double weight = position - static_cast<double>(lower);
    return sortedValues[lower] * (1.0 - weight) + sortedValues[upper] * weight;
}

std::vector<double> sortedFrameValues(const std::vector<double> &frame)
{
    std::vector<double> values = frame;
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<double> toDoubleFrame(const std::vector<uint8_t> &frame)
{
    std::vector<double> values;
    values.reserve(frame.size());
    for (const uint8_t value : frame)
    {
        values.push_back(static_cast<double>(value));
    }
    return values;
}

size_t clampedIndex(int y, int x)
{
    const int clampedY = std::clamp(y, 0, kTactileFrameHeight - 1);
    const int clampedX = std::clamp(x, 0, kTactileFrameWidth - 1);
    return static_cast<size_t>(clampedY) * kWidth + static_cast<size_t>(clampedX);
}

std::vector<double> shiftFrameWithEdge(const std::vector<double> &frame, int shiftY, int shiftX)
{
    std::vector<double> shifted(frame.size(), 0.0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            shifted[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] =
                frame[clampedIndex(y - shiftY, x - shiftX)];
        }
    }
    return shifted;
}

std::vector<double> boxBlurFrame(const std::vector<double> &frame, int radius)
{
    if (frame.size() != kTactileFrameBytes || radius <= 0)
    {
        return frame;
    }

    std::vector<double> horizontal(frame.size(), 0.0);
    std::vector<double> blurred(frame.size(), 0.0);
    const int diameter = radius * 2 + 1;
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        double sum = 0.0;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            sum += frame[clampedIndex(y, offset)];
        }
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            horizontal[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] =
                sum / static_cast<double>(diameter);
            sum -= frame[clampedIndex(y, x - radius)];
            sum += frame[clampedIndex(y, x + radius + 1)];
        }
    }

    for (int x = 0; x < kTactileFrameWidth; ++x)
    {
        double sum = 0.0;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            sum += horizontal[clampedIndex(offset, x)];
        }
        for (int y = 0; y < kTactileFrameHeight; ++y)
        {
            blurred[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] =
                sum / static_cast<double>(diameter);
            sum -= horizontal[clampedIndex(y - radius, x)];
            sum += horizontal[clampedIndex(y + radius + 1, x)];
        }
    }
    return blurred;
}

struct AlignmentResult
{
    std::vector<double> frame;
    int shiftX = 0;
    int shiftY = 0;
    double score = 0.0;
};

AlignmentResult alignIntegerTranslation(const std::vector<double> &baseline,
                                        const std::vector<double> &current)
{
    const std::vector<double> baselineBlur = boxBlurFrame(baseline, kTactileAlignmentBlurRadius);
    const std::vector<double> currentBlur = boxBlurFrame(current, kTactileAlignmentBlurRadius);
    constexpr int margin = kTactileAlignmentSearchRadius + 4;
    double bestScore = std::numeric_limits<double>::infinity();
    int bestShiftX = 0;
    int bestShiftY = 0;
    int bestDistance = 0;

    for (int shiftY = -kTactileAlignmentSearchRadius; shiftY <= kTactileAlignmentSearchRadius; ++shiftY)
    {
        for (int shiftX = -kTactileAlignmentSearchRadius; shiftX <= kTactileAlignmentSearchRadius; ++shiftX)
        {
            double sum = 0.0;
            size_t count = 0;
            for (int y = margin; y < kTactileFrameHeight - margin; ++y)
            {
                for (int x = margin; x < kTactileFrameWidth - margin; ++x)
                {
                    const double shiftedCurrent = currentBlur[clampedIndex(y - shiftY, x - shiftX)];
                    sum += std::fabs(baselineBlur[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] -
                                     shiftedCurrent);
                    ++count;
                }
            }
            const double score = count > 0 ? sum / static_cast<double>(count) : 0.0;
            const int distance = std::abs(shiftX) + std::abs(shiftY);
            if (score < bestScore - 1e-9 ||
                (std::fabs(score - bestScore) <= 1e-9 && distance < bestDistance))
            {
                bestScore = score;
                bestShiftX = shiftX;
                bestShiftY = shiftY;
                bestDistance = distance;
            }
        }
    }

    return {shiftFrameWithEdge(current, bestShiftY, bestShiftX), bestShiftX, bestShiftY, bestScore};
}

std::vector<uint8_t> edgeMask(const std::vector<double> &frame)
{
    std::vector<uint8_t> mask(frame.size(), 0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const double gx =
                -frame[clampedIndex(y - 1, x - 1)] + frame[clampedIndex(y - 1, x + 1)] -
                2.0 * frame[clampedIndex(y, x - 1)] + 2.0 * frame[clampedIndex(y, x + 1)] -
                frame[clampedIndex(y + 1, x - 1)] + frame[clampedIndex(y + 1, x + 1)];
            const double gy =
                -frame[clampedIndex(y - 1, x - 1)] - 2.0 * frame[clampedIndex(y - 1, x)] -
                frame[clampedIndex(y - 1, x + 1)] + frame[clampedIndex(y + 1, x - 1)] +
                2.0 * frame[clampedIndex(y + 1, x)] + frame[clampedIndex(y + 1, x + 1)];
            if (std::hypot(gx, gy) >= kTactileEdgeMagnitudeThreshold)
            {
                mask[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] = 1;
            }
        }
    }
    return mask;
}

std::vector<uint8_t> dilateMask(const std::vector<uint8_t> &mask, int radius)
{
    std::vector<uint8_t> dilated(mask.size(), 0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kWidth + static_cast<size_t>(x);
            for (int dy = -radius; dy <= radius && dilated[index] == 0; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    if (mask[clampedIndex(y + dy, x + dx)] != 0)
                    {
                        dilated[index] = 1;
                        break;
                    }
                }
            }
        }
    }
    return dilated;
}

std::vector<double> localSymmetricResidual(const std::vector<double> &baseline,
                                           const std::vector<double> &current,
                                           int radius)
{
    std::vector<double> residual(baseline.size(), 0.0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kWidth + static_cast<size_t>(x);
            double forward = std::numeric_limits<double>::infinity();
            double reverse = std::numeric_limits<double>::infinity();
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    forward = std::min(
                        forward, std::fabs(baseline[index] - current[clampedIndex(y + dy, x + dx)]));
                    reverse = std::min(
                        reverse, std::fabs(current[index] - baseline[clampedIndex(y + dy, x + dx)]));
                }
            }
            residual[index] = std::max(forward, reverse);
        }
    }
    return residual;
}

size_t countFilteredResidualMask(const std::vector<uint8_t> &rawMask,
                                 const std::vector<double> &lowFrequencyDiff,
                                 size_t *maxComponentPixels,
                                 size_t *droppedArtifactPixelsOut,
                                 size_t *droppedArtifactComponentsOut)
{
    std::vector<uint8_t> neighborMask(rawMask.size(), 0);
    for (int y = 0; y < kTactileFrameHeight; ++y)
    {
        for (int x = 0; x < kTactileFrameWidth; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kWidth + static_cast<size_t>(x);
            if (rawMask[index] == 0)
            {
                continue;
            }
            int neighborCount = 0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nextY = y + dy;
                    const int nextX = x + dx;
                    if (nextY >= 0 && nextY < kTactileFrameHeight &&
                        nextX >= 0 && nextX < kTactileFrameWidth &&
                        rawMask[static_cast<size_t>(nextY) * kWidth + static_cast<size_t>(nextX)] != 0)
                    {
                        ++neighborCount;
                    }
                }
            }
            if (neighborCount >= kTactileResidualMinNeighborCount)
            {
                neighborMask[index] = 1;
            }
        }
    }

    std::vector<uint8_t> visited(rawMask.size(), 0);
    std::vector<size_t> stack;
    size_t retainedPixels = 0;
    size_t maxComponent = 0;
    size_t droppedArtifactPixelCount = 0;
    size_t droppedArtifactComponents = 0;
    for (size_t start = 0; start < neighborMask.size(); ++start)
    {
        if (neighborMask[start] == 0 || visited[start] != 0)
        {
            continue;
        }

        stack.clear();
        stack.push_back(start);
        visited[start] = 1;
        size_t componentPixels = 0;
        double lowFrequencySum = 0.0;
        for (size_t cursor = 0; cursor < stack.size(); ++cursor)
        {
            const size_t index = stack[cursor];
            ++componentPixels;
            lowFrequencySum += lowFrequencyDiff[index];
            const int y = static_cast<int>(index / kWidth);
            const int x = static_cast<int>(index % kWidth);
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nextY = y + dy;
                    const int nextX = x + dx;
                    if (nextY < 0 || nextY >= kTactileFrameHeight ||
                        nextX < 0 || nextX >= kTactileFrameWidth)
                    {
                        continue;
                    }
                    const size_t next = static_cast<size_t>(nextY) * kWidth + static_cast<size_t>(nextX);
                    if (neighborMask[next] != 0 && visited[next] == 0)
                    {
                        visited[next] = 1;
                        stack.push_back(next);
                    }
                }
            }
        }

        maxComponent = std::max(maxComponent, componentPixels);
        if (componentPixels >= kTactileResidualMinComponentPixels)
        {
            const double lowFrequencyMean = lowFrequencySum / static_cast<double>(componentPixels);
            if (componentPixels <= kTactileResidualArtifactMaxComponentPixels &&
                lowFrequencyMean < kTactileResidualArtifactLowFrequencyMeanThreshold)
            {
                droppedArtifactPixelCount += componentPixels;
                ++droppedArtifactComponents;
            }
            else
            {
                retainedPixels += componentPixels;
            }
        }
    }

    if (maxComponentPixels != nullptr)
    {
        *maxComponentPixels = maxComponent;
    }
    if (droppedArtifactPixelsOut != nullptr)
    {
        *droppedArtifactPixelsOut = droppedArtifactPixelCount;
    }
    if (droppedArtifactComponentsOut != nullptr)
    {
        *droppedArtifactComponentsOut = droppedArtifactComponents;
    }
    return retainedPixels;
}

}  // namespace

TactileFrameMetrics computeTactileFrameMetrics(const std::vector<uint8_t> &baseline,
                                                const std::vector<uint8_t> &current)
{
    TactileFrameMetrics metrics;
    if (baseline.size() != kTactileFrameBytes || current.size() != kTactileFrameBytes)
    {
        metrics.damaged = true;
        metrics.robustResidualArea = 1.0;
        return metrics;
    }

    const std::vector<double> baselineValues = toDoubleFrame(baseline);
    const AlignmentResult alignment = alignIntegerTranslation(baselineValues, toDoubleFrame(current));
    metrics.alignmentShiftX = alignment.shiftX;
    metrics.alignmentShiftY = alignment.shiftY;
    metrics.alignmentScore = alignment.score;

    const auto baselineSorted = sortedFrameValues(baselineValues);
    const auto currentSorted = sortedFrameValues(alignment.frame);
    const double baselineP5 = percentileFromSorted(baselineSorted, 0.05);
    const double baselineP50 = percentileFromSorted(baselineSorted, 0.50);
    const double baselineP95 = percentileFromSorted(baselineSorted, 0.95);
    const double currentP5 = percentileFromSorted(currentSorted, 0.05);
    const double currentP50 = percentileFromSorted(currentSorted, 0.50);
    const double currentP95 = percentileFromSorted(currentSorted, 0.95);
    metrics.gain = (baselineP95 - baselineP5) / std::max(1e-6, currentP95 - currentP5);
    metrics.offset = baselineP50 - metrics.gain * currentP50;

    std::vector<double> correctedCurrentValues(alignment.frame.size(), 0.0);
    std::vector<double> directResiduals(alignment.frame.size(), 0.0);
    double residualSum = 0.0;
    for (size_t index = 0; index < alignment.frame.size(); ++index)
    {
        correctedCurrentValues[index] =
            std::clamp(alignment.frame[index] * metrics.gain + metrics.offset, 0.0, 255.0);
        directResiduals[index] = std::fabs(correctedCurrentValues[index] - baselineValues[index]);
        residualSum += directResiduals[index];
    }
    metrics.residualMean = residualSum / static_cast<double>(directResiduals.size());

    std::vector<double> sortedResiduals = sortedFrameValues(directResiduals);
    metrics.residualMedian = percentileFromSorted(sortedResiduals, 0.50);
    metrics.residualP99 = percentileFromSorted(sortedResiduals, 0.99);
    std::vector<double> absoluteMedianDeviations;
    absoluteMedianDeviations.reserve(directResiduals.size());
    for (const double residual : directResiduals)
    {
        absoluteMedianDeviations.push_back(std::fabs(residual - metrics.residualMedian));
    }
    std::sort(absoluteMedianDeviations.begin(), absoluteMedianDeviations.end());
    metrics.residualMad = percentileFromSorted(absoluteMedianDeviations, 0.50);
    metrics.residualThreshold = std::max(
        kTactileResidualFloorThreshold,
        metrics.residualMedian + kTactileResidualMadMultiplier * kTactileMadToSigma * metrics.residualMad);

    const std::vector<double> baselineLowFrequency =
        boxBlurFrame(baselineValues, kTactileResidualLowFrequencyBlurRadius);
    const std::vector<double> currentLowFrequency =
        boxBlurFrame(correctedCurrentValues, kTactileResidualLowFrequencyBlurRadius);
    std::vector<double> lowFrequencyDiff(baseline.size(), 0.0);
    for (size_t index = 0; index < baseline.size(); ++index)
    {
        lowFrequencyDiff[index] = std::fabs(currentLowFrequency[index] - baselineLowFrequency[index]);
    }

    const std::vector<double> localResidual = localSymmetricResidual(
        baselineValues, correctedCurrentValues, kTactileEdgeLocalToleranceRadius);
    const std::vector<uint8_t> baselineEdges =
        dilateMask(edgeMask(baselineValues), kTactileEdgeLocalToleranceRadius);
    const std::vector<uint8_t> currentEdges =
        dilateMask(edgeMask(correctedCurrentValues), kTactileEdgeLocalToleranceRadius);

    std::vector<uint8_t> rawResidualMask(directResiduals.size(), 0);
    size_t rawResidualMaskCount = 0;
    for (size_t index = 0; index < directResiduals.size(); ++index)
    {
        const bool shiftLike = baselineEdges[index] != 0 && currentEdges[index] != 0 &&
                               localResidual[index] < metrics.residualThreshold &&
                               lowFrequencyDiff[index] < kTactileEdgeLowFrequencyThreshold;
        if (directResiduals[index] >= metrics.residualThreshold && shiftLike)
        {
            ++metrics.edgeToleranceSuppressedPixels;
        }
        else if (directResiduals[index] >= metrics.residualThreshold)
        {
            rawResidualMask[index] = 1;
            ++rawResidualMaskCount;
        }
    }
    metrics.rawResidualArea =
        static_cast<double>(rawResidualMaskCount) / static_cast<double>(baseline.size());

    const size_t filteredResidualMaskCount = countFilteredResidualMask(
        rawResidualMask,
        lowFrequencyDiff,
        &metrics.maxResidualComponentPixels,
        &metrics.droppedArtifactComponentPixels,
        &metrics.droppedArtifactComponentCount);
    metrics.robustResidualArea =
        static_cast<double>(filteredResidualMaskCount) / static_cast<double>(baseline.size());
    metrics.damaged = metrics.robustResidualArea >= kTactileRobustResidualAreaThreshold;
    return metrics;
}

std::string formatTactileMetrics(const TactileFrameMetrics &metrics)
{
    return "robust_residual_area=" + formatFixed(metrics.robustResidualArea, 4) +
           " area_threshold=" + formatFixed(kTactileRobustResidualAreaThreshold, 4) +
           " raw_residual_area=" + formatFixed(metrics.rawResidualArea, 4) +
           " residual_thr=" + formatFixed(metrics.residualThreshold, 2) +
           " residual_median=" + formatFixed(metrics.residualMedian, 2) +
           " residual_mad=" + formatFixed(metrics.residualMad, 2) +
           " residual_p99=" + formatFixed(metrics.residualP99, 2) +
           " align_dx=" + std::to_string(metrics.alignmentShiftX) +
           " align_dy=" + std::to_string(metrics.alignmentShiftY) +
           " align_score=" + formatFixed(metrics.alignmentScore, 2) +
           " edge_tolerance_drop_px=" + std::to_string(metrics.edgeToleranceSuppressedPixels) +
           " max_component_px=" + std::to_string(metrics.maxResidualComponentPixels) +
           " min_component_px=" + std::to_string(kTactileResidualMinComponentPixels) +
           " artifact_drop_px=" + std::to_string(metrics.droppedArtifactComponentPixels) +
           " artifact_drop_components=" + std::to_string(metrics.droppedArtifactComponentCount) +
           " artifact_max_component_px=" + std::to_string(kTactileResidualArtifactMaxComponentPixels) +
           " artifact_lowfreq_mean_thr=" +
           formatFixed(kTactileResidualArtifactLowFrequencyMeanThreshold, 2) +
           " gain=" + formatFixed(metrics.gain, 4) +
           " offset=" + formatFixed(metrics.offset, 2);
}

}  // namespace DA::Ugripper::detail
