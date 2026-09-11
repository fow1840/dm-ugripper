#include "record_runtime/tactile_residual.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readFrame(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),
                                std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "usage: tactile_residual_benchmark BASELINE.gray CURRENT.gray [LOOPS]\n";
        return 1;
    }
    const int loops = argc == 4 ? std::max(1, std::stoi(argv[3])) : 100;
    const std::vector<uint8_t> baseline = readFrame(argv[1]);
    const std::vector<uint8_t> current = readFrame(argv[2]);
    if (baseline.size() != DA::Ugripper::detail::kTactileFrameBytes ||
        current.size() != DA::Ugripper::detail::kTactileFrameBytes)
    {
        std::cerr << "expected two 160x120 gray frames\n";
        return 1;
    }

    (void)DA::Ugripper::detail::computeTactileFrameMetrics(baseline, current);
    std::vector<double> elapsedMs;
    elapsedMs.reserve(static_cast<size_t>(loops));
    DA::Ugripper::detail::TactileFrameMetrics metrics;
    const auto totalStart = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < loops; ++iteration)
    {
        const auto start = std::chrono::steady_clock::now();
        metrics = DA::Ugripper::detail::computeTactileFrameMetrics(baseline, current);
        const auto finish = std::chrono::steady_clock::now();
        elapsedMs.push_back(
            std::chrono::duration<double, std::milli>(finish - start).count());
    }
    const auto totalFinish = std::chrono::steady_clock::now();
    std::sort(elapsedMs.begin(), elapsedMs.end());
    double sum = 0.0;
    for (const double value : elapsedMs)
    {
        sum += value;
    }
    const size_t p95Index = static_cast<size_t>(0.95 * static_cast<double>(loops - 1));
    std::cout << std::fixed << std::setprecision(3)
              << "loops=" << loops
              << " total_ms="
              << std::chrono::duration<double, std::milli>(totalFinish - totalStart).count()
              << " avg_ms=" << sum / static_cast<double>(loops)
              << " p95_ms=" << elapsedMs[p95Index]
              << " max_ms=" << elapsedMs.back()
              << " damaged=" << (metrics.damaged ? "true" : "false")
              << " reason="
              << (!metrics.baselineCompatible
                      ? "baseline_mismatch"
                      : (metrics.largeStructuralDamage
                             ? "large_structural_damage"
                             : (metrics.damaged ? "solid_core" : "clean")))
              << " tone_lut=" << (metrics.toneLutApplied ? "true" : "false")
              << " solid_core_px=" << metrics.solidCorePixels
              << " baseline_corr=" << metrics.baselineCorrelation
              << " block_match_ratio=" << metrics.baselineMatchingBlockRatio
              << '\n';
    return 0;
}
