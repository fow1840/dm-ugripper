#include "record_runtime/tactile_residual.h"

#include <algorithm>
#include <array>
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

double frameMean(const std::vector<double> &frame)
{
    double sum = 0.0;
    for (const double value : frame)
    {
        sum += value;
    }
    return frame.empty() ? 0.0 : sum / static_cast<double>(frame.size());
}

double pearsonCorrelation(const std::vector<double> &first,
                          const std::vector<double> &second)
{
    if (first.size() != second.size() || first.empty())
    {
        return 0.0;
    }

    const double firstMean = frameMean(first);
    const double secondMean = frameMean(second);
    double covariance = 0.0;
    double firstVariance = 0.0;
    double secondVariance = 0.0;
    for (size_t index = 0; index < first.size(); ++index)
    {
        const double firstCentered = first[index] - firstMean;
        const double secondCentered = second[index] - secondMean;
        covariance += firstCentered * secondCentered;
        firstVariance += firstCentered * firstCentered;
        secondVariance += secondCentered * secondCentered;
    }
    const double denominator = std::sqrt(firstVariance * secondVariance);
    return denominator > 1e-12 ? covariance / denominator : 0.0;
}

double blockCorrelation(const std::vector<double> &first,
                        const std::vector<double> &second,
                        int xBegin,
                        int xEnd,
                        int yBegin,
                        int yEnd)
{
    double firstSum = 0.0;
    double secondSum = 0.0;
    size_t count = 0;
    for (int y = yBegin; y < yEnd; ++y)
    {
        for (int x = xBegin; x < xEnd; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kWidth + static_cast<size_t>(x);
            firstSum += first[index];
            secondSum += second[index];
            ++count;
        }
    }
    if (count == 0)
    {
        return 0.0;
    }

    const double firstMean = firstSum / static_cast<double>(count);
    const double secondMean = secondSum / static_cast<double>(count);
    double covariance = 0.0;
    double firstVariance = 0.0;
    double secondVariance = 0.0;
    for (int y = yBegin; y < yEnd; ++y)
    {
        for (int x = xBegin; x < xEnd; ++x)
        {
            const size_t index = static_cast<size_t>(y) * kWidth + static_cast<size_t>(x);
            const double firstCentered = first[index] - firstMean;
            const double secondCentered = second[index] - secondMean;
            covariance += firstCentered * secondCentered;
            firstVariance += firstCentered * firstCentered;
            secondVariance += secondCentered * secondCentered;
        }
    }
    const double denominator = std::sqrt(firstVariance * secondVariance);
    return denominator > 1e-12 ? covariance / denominator : 0.0;
}

double matchingBlockRatio(const std::vector<double> &baseline,
                          const std::vector<double> &candidate)
{
    size_t matchingBlocks = 0;
    size_t totalBlocks = 0;
    for (int blockY = 0; blockY < kTactileBaselineBlockRows; ++blockY)
    {
        const int yBegin = blockY * kTactileFrameHeight / kTactileBaselineBlockRows;
        const int yEnd = (blockY + 1) * kTactileFrameHeight / kTactileBaselineBlockRows;
        for (int blockX = 0; blockX < kTactileBaselineBlockColumns; ++blockX)
        {
            const int xBegin = blockX * kTactileFrameWidth / kTactileBaselineBlockColumns;
            const int xEnd = (blockX + 1) * kTactileFrameWidth / kTactileBaselineBlockColumns;
            if (blockCorrelation(baseline, candidate, xBegin, xEnd, yBegin, yEnd) >=
                kTactileBaselineBlockMinCorrelation)
            {
                ++matchingBlocks;
            }
            ++totalBlocks;
        }
    }
    return totalBlocks == 0
               ? 0.0
               : static_cast<double>(matchingBlocks) / static_cast<double>(totalBlocks);
}

std::vector<double> quantileToneMap(const std::vector<double> &frame,
                                    const std::vector<double> &frameSorted,
                                    const std::vector<double> &baselineSorted)
{
    constexpr std::array<double, 9> kQuantiles = {
        0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99};
    std::array<double, kQuantiles.size()> sourcePoints{};
    std::array<double, kQuantiles.size()> targetPoints{};
    for (size_t index = 0; index < kQuantiles.size(); ++index)
    {
        sourcePoints[index] = percentileFromSorted(frameSorted, kQuantiles[index]);
        targetPoints[index] = percentileFromSorted(baselineSorted, kQuantiles[index]);
    }

    std::vector<std::pair<double, double>> uniquePoints;
    uniquePoints.reserve(kQuantiles.size());
    for (size_t index = 0; index < kQuantiles.size(); ++index)
    {
        if (uniquePoints.empty() || sourcePoints[index] != uniquePoints.back().first)
        {
            uniquePoints.emplace_back(sourcePoints[index], targetPoints[index]);
        }
    }

    std::vector<double> mapped(frame.size(), 0.0);
    if (uniquePoints.size() == 1)
    {
        std::fill(mapped.begin(), mapped.end(), uniquePoints.front().second);
        return mapped;
    }

    for (size_t index = 0; index < frame.size(); ++index)
    {
        const double value = frame[index];
        if (value <= uniquePoints.front().first)
        {
            mapped[index] = uniquePoints.front().second;
            continue;
        }
        if (value >= uniquePoints.back().first)
        {
            mapped[index] = uniquePoints.back().second;
            continue;
        }
        const auto upper = std::upper_bound(
            uniquePoints.begin(), uniquePoints.end(), value,
            [](double candidate, const std::pair<double, double> &point) {
                return candidate < point.first;
            });
        const auto lower = upper - 1;
        const double weight =
            (value - lower->first) / std::max(1e-12, upper->first - lower->first);
        mapped[index] = lower->second * (1.0 - weight) + upper->second * weight;
    }
    return mapped;
}

struct ResidualEvaluation
{
    std::vector<double> corrected;
    std::vector<double> residuals;
    double gain = 1.0;
    double offset = 0.0;
    double residualMean = 0.0;
    double residualMedian = 0.0;
    double residualMad = 0.0;
    double residualP99 = 0.0;
    double residualThreshold = 0.0;
    double correlation = 0.0;
};

ResidualEvaluation evaluateResidual(const std::vector<double> &baseline,
                                    const std::vector<double> &baselineSorted,
                                    const std::vector<double> &candidate)
{
    ResidualEvaluation evaluation;
    const std::vector<double> candidateSorted = sortedFrameValues(candidate);
    const double baselineP5 = percentileFromSorted(baselineSorted, 0.05);
    const double baselineP50 = percentileFromSorted(baselineSorted, 0.50);
    const double baselineP95 = percentileFromSorted(baselineSorted, 0.95);
    const double candidateP5 = percentileFromSorted(candidateSorted, 0.05);
    const double candidateP50 = percentileFromSorted(candidateSorted, 0.50);
    const double candidateP95 = percentileFromSorted(candidateSorted, 0.95);
    evaluation.gain =
        (baselineP95 - baselineP5) / std::max(1e-6, candidateP95 - candidateP5);
    evaluation.offset = baselineP50 - evaluation.gain * candidateP50;
    evaluation.correlation = pearsonCorrelation(baseline, candidate);

    evaluation.corrected.resize(candidate.size(), 0.0);
    evaluation.residuals.resize(candidate.size(), 0.0);
    double residualSum = 0.0;
    for (size_t index = 0; index < candidate.size(); ++index)
    {
        evaluation.corrected[index] =
            std::clamp(candidate[index] * evaluation.gain + evaluation.offset, 0.0, 255.0);
        evaluation.residuals[index] =
            std::fabs(evaluation.corrected[index] - baseline[index]);
        residualSum += evaluation.residuals[index];
    }
    evaluation.residualMean = residualSum / static_cast<double>(evaluation.residuals.size());

    const std::vector<double> sortedResiduals = sortedFrameValues(evaluation.residuals);
    evaluation.residualMedian = percentileFromSorted(sortedResiduals, 0.50);
    evaluation.residualP99 = percentileFromSorted(sortedResiduals, 0.99);
    std::vector<double> absoluteMedianDeviations;
    absoluteMedianDeviations.reserve(evaluation.residuals.size());
    for (const double residual : evaluation.residuals)
    {
        absoluteMedianDeviations.push_back(std::fabs(residual - evaluation.residualMedian));
    }
    std::sort(absoluteMedianDeviations.begin(), absoluteMedianDeviations.end());
    evaluation.residualMad = percentileFromSorted(absoluteMedianDeviations, 0.50);
    evaluation.residualThreshold = std::max(
        kTactileResidualFloorThreshold,
        evaluation.residualMedian +
            kTactileResidualMadMultiplier * kTactileMadToSigma * evaluation.residualMad);
    return evaluation;
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
    if constexpr (kTactileAlignmentSearchRadius <= 0)
    {
        return {current, 0, 0, 0.0};
    }

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

struct SolidCoreResult
{
    size_t solidCorePixels = 0;
    size_t maxComponentPixels = 0;
    size_t maxSolidCoreComponentPixels = 0;
    size_t droppedCurvePixels = 0;
    size_t droppedCurveComponents = 0;
};

SolidCoreResult countSolidCorePixels(const std::vector<uint8_t> &rawMask)
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

    std::vector<uint8_t> coreMask(rawMask.size(), 0);
    for (int y = 1; y < kTactileFrameHeight - 1; ++y)
    {
        for (int x = 1; x < kTactileFrameWidth - 1; ++x)
        {
            bool solid = true;
            for (int dy = -1; dy <= 1 && solid; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (neighborMask[static_cast<size_t>(y + dy) * kWidth +
                                     static_cast<size_t>(x + dx)] == 0)
                    {
                        solid = false;
                        break;
                    }
                }
            }
            if (solid)
            {
                coreMask[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)] = 1;
            }
        }
    }

    std::vector<uint8_t> visited(rawMask.size(), 0);
    std::vector<size_t> stack;
    SolidCoreResult result;
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
        size_t componentCorePixels = 0;
        for (size_t cursor = 0; cursor < stack.size(); ++cursor)
        {
            const size_t index = stack[cursor];
            ++componentPixels;
            componentCorePixels += coreMask[index] != 0 ? 1U : 0U;
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

        result.maxComponentPixels = std::max(result.maxComponentPixels, componentPixels);
        result.maxSolidCoreComponentPixels =
            std::max(result.maxSolidCoreComponentPixels, componentCorePixels);
        if (componentPixels >= kTactileResidualMinComponentPixels)
        {
            if (componentCorePixels < kTactileResidualMinSolidCorePixels)
            {
                result.droppedCurvePixels += componentPixels;
                ++result.droppedCurveComponents;
            }
            else
            {
                result.solidCorePixels += componentCorePixels;
            }
        }
    }
    return result;
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
        metrics.solidCorePixels = kTactileFrameBytes;
        return metrics;
    }

    const std::vector<double> baselineValues = toDoubleFrame(baseline);
    const AlignmentResult alignment = alignIntegerTranslation(baselineValues, toDoubleFrame(current));
    metrics.alignmentShiftX = alignment.shiftX;
    metrics.alignmentShiftY = alignment.shiftY;
    metrics.alignmentScore = alignment.score;

    const std::vector<double> baselineSorted = sortedFrameValues(baselineValues);
    const std::vector<double> currentSorted = sortedFrameValues(alignment.frame);
    const ResidualEvaluation affineEvaluation =
        evaluateResidual(baselineValues, baselineSorted, alignment.frame);
    metrics.affineResidualP99 = affineEvaluation.residualP99;
    const double meanShift = std::fabs(frameMean(alignment.frame) - frameMean(baselineValues));
    metrics.toneLutAttempted = meanShift >= kTactileToneLutMinMeanShift;
    std::vector<double> toneMappedCurrent;
    ResidualEvaluation toneEvaluation;
    if (metrics.toneLutAttempted)
    {
        toneMappedCurrent = quantileToneMap(alignment.frame, currentSorted, baselineSorted);
        toneEvaluation = evaluateResidual(baselineValues, baselineSorted, toneMappedCurrent);
        metrics.toneLutResidualP99 = toneEvaluation.residualP99;
        metrics.toneLutCorrelationGain =
            toneEvaluation.correlation - affineEvaluation.correlation;
        metrics.toneLutApplied =
            toneEvaluation.residualP99 <= kTactileToneLutMaxResidualP99 &&
            toneEvaluation.residualP99 <=
                affineEvaluation.residualP99 * kTactileToneLutMaxP99Ratio &&
            metrics.toneLutCorrelationGain > 0.0;
    }
    else
    {
        metrics.toneLutResidualP99 = affineEvaluation.residualP99;
    }
    const ResidualEvaluation &selectedEvaluation =
        metrics.toneLutApplied ? toneEvaluation : affineEvaluation;
    metrics.baselineCorrelation = selectedEvaluation.correlation;
    const std::vector<double> &selectedCandidate =
        metrics.toneLutApplied ? toneMappedCurrent : alignment.frame;
    metrics.baselineMatchingBlockRatio =
        matchingBlockRatio(baselineValues, selectedCandidate);
    metrics.gain = selectedEvaluation.gain;
    metrics.offset = selectedEvaluation.offset;
    metrics.residualMean = selectedEvaluation.residualMean;
    metrics.residualMedian = selectedEvaluation.residualMedian;
    metrics.residualMad = selectedEvaluation.residualMad;
    metrics.residualP99 = selectedEvaluation.residualP99;
    metrics.residualThreshold = selectedEvaluation.residualThreshold;
    const std::vector<double> &correctedCurrentValues = selectedEvaluation.corrected;
    const std::vector<double> &directResiduals = selectedEvaluation.residuals;

    const bool lowGlobalCorrelation =
        metrics.baselineCorrelation < kTactileBaselineMinCorrelation;
    metrics.baselineCompatible =
        !lowGlobalCorrelation ||
        metrics.baselineMatchingBlockRatio >
            kTactileBaselineMismatchMaxMatchingBlockRatio;
    metrics.baselineRefreshRequired = !metrics.baselineCompatible;
    if (!metrics.baselineCompatible)
    {
        metrics.rawResidualArea = 0.0;
        metrics.robustResidualArea = 0.0;
        metrics.maxResidualComponentPixels = 0;
        metrics.maxSolidCoreComponentPixels = 0;
        metrics.solidCorePixels = 0;
        metrics.damaged = false;
        return metrics;
    }
    if (lowGlobalCorrelation)
    {
        metrics.largeStructuralDamage = true;
        metrics.rawResidualArea =
            1.0 - metrics.baselineMatchingBlockRatio;
        metrics.robustResidualArea = metrics.rawResidualArea;
        metrics.solidCorePixels = static_cast<size_t>(std::lround(
            metrics.robustResidualArea * static_cast<double>(kTactileFrameBytes)));
        metrics.maxResidualComponentPixels = metrics.solidCorePixels;
        metrics.maxSolidCoreComponentPixels = metrics.solidCorePixels;
        metrics.damaged = true;
        return metrics;
    }

    std::vector<double> lowFrequencyDiff(baseline.size(), 0.0);
    std::vector<double> localResidual;
    std::vector<uint8_t> baselineEdges;
    std::vector<uint8_t> currentEdges;
    if constexpr (kTactileEdgeLocalToleranceRadius > 0)
    {
        const std::vector<double> baselineLowFrequency = boxBlurFrame(baselineValues, 2);
        const std::vector<double> currentLowFrequency = boxBlurFrame(correctedCurrentValues, 2);
        for (size_t index = 0; index < baseline.size(); ++index)
        {
            lowFrequencyDiff[index] =
                std::fabs(currentLowFrequency[index] - baselineLowFrequency[index]);
        }
        localResidual = localSymmetricResidual(
            baselineValues, correctedCurrentValues, kTactileEdgeLocalToleranceRadius);
        baselineEdges = dilateMask(edgeMask(baselineValues), kTactileEdgeLocalToleranceRadius);
        currentEdges = dilateMask(edgeMask(correctedCurrentValues), kTactileEdgeLocalToleranceRadius);
    }

    std::vector<uint8_t> rawResidualMask(directResiduals.size(), 0);
    size_t rawResidualMaskCount = 0;
    for (size_t index = 0; index < directResiduals.size(); ++index)
    {
        bool shiftLike = false;
        if constexpr (kTactileEdgeLocalToleranceRadius > 0)
        {
            shiftLike = baselineEdges[index] != 0 && currentEdges[index] != 0 &&
                        localResidual[index] < metrics.residualThreshold &&
                        lowFrequencyDiff[index] < kTactileEdgeLowFrequencyThreshold;
        }
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

    const SolidCoreResult coreResult = countSolidCorePixels(rawResidualMask);
    metrics.maxResidualComponentPixels = coreResult.maxComponentPixels;
    metrics.maxSolidCoreComponentPixels = coreResult.maxSolidCoreComponentPixels;
    metrics.solidCorePixels = coreResult.solidCorePixels;
    metrics.droppedCurvePixels = coreResult.droppedCurvePixels;
    metrics.droppedCurveComponentCount = coreResult.droppedCurveComponents;
    metrics.robustResidualArea =
        static_cast<double>(metrics.solidCorePixels) / static_cast<double>(baseline.size());
    metrics.damaged = metrics.solidCorePixels >= kTactileResidualMinSolidCorePixels;
    return metrics;
}

std::string formatTactileMetrics(const TactileFrameMetrics &metrics)
{
    return "robust_residual_area=" + formatFixed(metrics.robustResidualArea, 4) +
           " solid_core_px=" + std::to_string(metrics.solidCorePixels) +
           " min_solid_core_px=" + std::to_string(kTactileResidualMinSolidCorePixels) +
           " raw_residual_area=" + formatFixed(metrics.rawResidualArea, 4) +
           " residual_thr=" + formatFixed(metrics.residualThreshold, 2) +
           " residual_median=" + formatFixed(metrics.residualMedian, 2) +
           " residual_mad=" + formatFixed(metrics.residualMad, 2) +
           " residual_p99=" + formatFixed(metrics.residualP99, 2) +
           " affine_residual_p99=" + formatFixed(metrics.affineResidualP99, 2) +
           " tone_lut_residual_p99=" + formatFixed(metrics.toneLutResidualP99, 2) +
           " tone_lut_corr_gain=" + formatFixed(metrics.toneLutCorrelationGain, 6) +
           " tone_lut_attempted=" +
           std::string(metrics.toneLutAttempted ? "true" : "false") +
           " tone_lut_applied=" + std::string(metrics.toneLutApplied ? "true" : "false") +
           " baseline_corr=" + formatFixed(metrics.baselineCorrelation, 6) +
           " baseline_matching_block_ratio=" +
           formatFixed(metrics.baselineMatchingBlockRatio, 4) +
           " baseline_compatible=" +
           std::string(metrics.baselineCompatible ? "true" : "false") +
           " baseline_refresh_required=" +
           std::string(metrics.baselineRefreshRequired ? "true" : "false") +
           " large_structural_damage=" +
           std::string(metrics.largeStructuralDamage ? "true" : "false") +
           " align_dx=" + std::to_string(metrics.alignmentShiftX) +
           " align_dy=" + std::to_string(metrics.alignmentShiftY) +
           " align_score=" + formatFixed(metrics.alignmentScore, 2) +
           " edge_tolerance_drop_px=" + std::to_string(metrics.edgeToleranceSuppressedPixels) +
           " max_component_px=" + std::to_string(metrics.maxResidualComponentPixels) +
           " max_solid_core_component_px=" +
           std::to_string(metrics.maxSolidCoreComponentPixels) +
           " min_component_px=" + std::to_string(kTactileResidualMinComponentPixels) +
           " curve_drop_px=" + std::to_string(metrics.droppedCurvePixels) +
           " curve_drop_components=" + std::to_string(metrics.droppedCurveComponentCount) +
           " gain=" + formatFixed(metrics.gain, 4) +
           " offset=" + formatFixed(metrics.offset, 2);
}

}  // namespace DA::Ugripper::detail
