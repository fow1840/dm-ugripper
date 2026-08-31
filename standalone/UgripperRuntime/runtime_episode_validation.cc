#include "record_runtime.h"
#include "record_runtime/runtime_episode_internal.h"
#include "main_camera_calibration_data.h"
#include "record_runtime/error_code_mapping.h"
#include "utils/ugripper_logger.h"
#include "record_runtime/gripper_refresh_logic.h"
#include "utils/env_utils.h"
#include "utils/file_utils.hpp"
#include "utils/time_utils.h"

#include <mcap/reader.hpp>
#include <nlohmann/json.hpp>
#include <uuid/uuid.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <utility>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

using namespace ugripper::runtime::record_runtime_detail;

namespace ugripper::runtime::record_runtime_detail {

fs::path tactileReferenceRawPath(const fs::path &root, const std::string &serial)
{
    return tactileReferenceDir(root) / (sanitizeFileComponent(serial) + ".gray");
}

fs::path tactileReferenceMetaPath(const fs::path &root, const std::string &serial)
{
    return tactileReferenceDir(root) / (sanitizeFileComponent(serial) + ".json");
}

fs::path tactilePersistentRawPath(const fs::path &root, const std::string &serial)
{
    return tactilePersistentDir(root) / (sanitizeFileComponent(serial) + ".gray");
}

std::string currentBootId()
{
    std::ifstream input("/proc/sys/kernel/random/boot_id");
    std::string bootId;
    if (input.is_open())
    {
        std::getline(input, bootId);
    }
    return bootId;
}

bool writeTactileReferenceMeta(const fs::path &root,
                               const std::string &serial,
                               const TactileCalibrationTarget &target,
                               uint64_t updatedAtMs,
                               std::string *errorMessage)
{
    json meta = {
        {"serial", serial},
        {"camera_name", target.cameraName},
        {"device_path", target.devicePath},
        {"updated_at_ms", updatedAtMs},
    };
    return writeTextFileAtomically(
        tactileReferenceMetaPath(root, serial), meta.dump(2) + "\n", errorMessage);
}

std::string formatFixed(double value, int precision)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

bool isStereoCameraName(const char *cameraName)
{
    return cameraName != nullptr && std::strstr(cameraName, "stereo") != nullptr;
}

std::vector<EpisodeVideoArtifact> activeEpisodeVideoArtifacts(bool chestCameraEnabled,
                                                              bool stereoEnabled)
{
    std::vector<EpisodeVideoArtifact> artifacts;
    artifacts.reserve(kAllEpisodeVideoArtifacts.size());
    for (const auto &artifact : kAllEpisodeVideoArtifacts)
    {
        if (!chestCameraEnabled && std::strcmp(artifact.cameraName, "chest_cam_main") == 0)
        {
            continue;
        }
        if (!stereoEnabled && isStereoCameraName(artifact.cameraName))
        {
            continue;
        }
        artifacts.push_back(artifact);
    }
    return artifacts;
}

const char *episodeVideoFileNameForCamera(const char *cameraName)
{
    for (const auto &artifact : kAllEpisodeVideoArtifacts)
    {
        if (std::strcmp(artifact.cameraName, cameraName) == 0)
        {
            return artifact.fileName;
        }
    }
    return nullptr;
}

std::string currentDateString()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&nowTime, &localTime);

    char dateBuffer[16] = {0};
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &localTime);
    return std::string(dateBuffer);
}

std::set<std::string> loadPersistentResetSerials(const fs::path &path)
{
    std::set<std::string> result;
    std::ifstream input(path);
    if (!input.is_open())
    {
        return result;
    }

    try
    {
        json root = json::parse(input);
        if (root.contains("serials") && root["serials"].is_array())
        {
            for (const auto &entry : root["serials"])
            {
                if (entry.is_string() && !entry.get<std::string>().empty())
                {
                    result.insert(entry.get<std::string>());
                }
            }
        }
    }
    catch (const std::exception &ex)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to parse tactile persistent reset list: " << ex.what() << std::endl).str());
    }
    return result;
}

bool storePersistentResetSerials(const fs::path &path,
                                 const std::set<std::string> &serials,
                                 std::string *errorMessage)
{
    json root = {
        {"serials", json::array()},
    };
    for (const auto &serial : serials)
    {
        root["serials"].push_back(serial);
    }
    return writeTextFileAtomically(path, root.dump(2) + "\n", errorMessage);
}

bool writeBinaryFile(const fs::path &path, const std::vector<uint8_t> &bytes, std::string *errorMessage)
{
    const fs::path parent = path.parent_path();
    std::error_code error;
    if (!parent.empty())
    {
        fs::create_directories(parent, error);
        if (error)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "create parent directory failed: " + error.message();
            }
            return false;
        }
    }

    const fs::path tempPath = path.string() + ".tmp";
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "cannot open temp file for write: " + tempPath.string();
        }
        return false;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output.good())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to write temp file: " + tempPath.string();
        }
        output.close();
        fs::remove(tempPath, error);
        return false;
    }
    output.close();

    fs::rename(tempPath, path, error);
    if (error)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "rename temp file failed: " + error.message();
        }
        fs::remove(tempPath, error);
        return false;
    }
    return true;
}

bool readBinaryFileExact(const fs::path &path,
                         size_t expectedBytes,
                         std::vector<uint8_t> *bytes,
                         std::string *errorMessage)
{
    if (bytes == nullptr)
    {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "cannot open file: " + path.string();
        }
        return false;
    }

    std::vector<uint8_t> buffer(expectedBytes, 0);
    input.read(reinterpret_cast<char *>(buffer.data()),
               static_cast<std::streamsize>(expectedBytes));
    const std::streamsize actual = input.gcount();
    if (actual != static_cast<std::streamsize>(expectedBytes) ||
        input.peek() != std::ifstream::traits_type::eof())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "unexpected tactile frame size: " + path.string();
        }
        return false;
    }
    *bytes = std::move(buffer);
    return true;
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

std::vector<double> sortedFrameValues(const std::vector<uint8_t> &frame)
{
    std::vector<double> values;
    values.reserve(frame.size());
    for (const uint8_t value : frame)
    {
        values.push_back(static_cast<double>(value));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<double> boxBlurFrame(const std::vector<double> &frame,
                                 size_t width,
                                 size_t height,
                                 int radius)
{
    std::vector<double> blurred(frame.size(), 0.0);
    if (frame.size() != width * height || frame.empty() || radius <= 0)
    {
        return frame;
    }

    const int radiusValue = std::max(0, radius);
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            double sum = 0.0;
            size_t count = 0;
            const size_t yBegin = static_cast<size_t>(
                std::max<int64_t>(0, static_cast<int64_t>(y) - radiusValue));
            const size_t yEnd = static_cast<size_t>(
                std::min<int64_t>(static_cast<int64_t>(height) - 1, static_cast<int64_t>(y) + radiusValue));
            const size_t xBegin = static_cast<size_t>(
                std::max<int64_t>(0, static_cast<int64_t>(x) - radiusValue));
            const size_t xEnd = static_cast<size_t>(
                std::min<int64_t>(static_cast<int64_t>(width) - 1, static_cast<int64_t>(x) + radiusValue));
            for (size_t yy = yBegin; yy <= yEnd; ++yy)
            {
                for (size_t xx = xBegin; xx <= xEnd; ++xx)
                {
                    sum += frame[yy * width + xx];
                    ++count;
                }
            }
            blurred[y * width + x] = count > 0 ? sum / static_cast<double>(count) : frame[y * width + x];
        }
    }
    return blurred;
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
           " max_component_px=" + std::to_string(metrics.maxResidualComponentPixels) +
           " min_component_px=" + std::to_string(kTactileResidualMinComponentPixels) +
           " artifact_drop_px=" + std::to_string(metrics.droppedArtifactComponentPixels) +
           " artifact_drop_components=" + std::to_string(metrics.droppedArtifactComponentCount) +
           " artifact_max_component_px=" + std::to_string(kTactileResidualArtifactMaxComponentPixels) +
           " artifact_lowfreq_mean_thr=" + formatFixed(kTactileResidualArtifactLowFrequencyMeanThreshold, 2) +
           " gain=" + formatFixed(metrics.gain, 4) +
           " offset=" + formatFixed(metrics.offset, 2);
}

size_t countFilteredResidualMask(const std::vector<uint8_t> &rawMask,
                                 size_t width,
                                 size_t height,
                                 size_t minComponentPixels,
                                 const std::vector<double> *lowFrequencyDiff,
                                 size_t *maxComponentPixels,
                                 size_t *droppedArtifactPixelsOut,
                                 size_t *droppedArtifactComponentsOut)
{
    if (rawMask.size() != width * height || rawMask.empty())
    {
        if (maxComponentPixels != nullptr)
        {
            *maxComponentPixels = 0;
        }
        if (droppedArtifactPixelsOut != nullptr)
        {
            *droppedArtifactPixelsOut = 0;
        }
        if (droppedArtifactComponentsOut != nullptr)
        {
            *droppedArtifactComponentsOut = 0;
        }
        return 0;
    }

    std::vector<uint8_t> neighborMask(rawMask.size(), 0);
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const size_t index = y * width + x;
            if (rawMask[index] == 0)
            {
                continue;
            }
            int neighborCount = 0;
            const size_t yBegin = (y == 0) ? 0 : y - 1;
            const size_t yEnd = std::min(height - 1, y + 1);
            const size_t xBegin = (x == 0) ? 0 : x - 1;
            const size_t xEnd = std::min(width - 1, x + 1);
            for (size_t yy = yBegin; yy <= yEnd; ++yy)
            {
                for (size_t xx = xBegin; xx <= xEnd; ++xx)
                {
                    if (rawMask[yy * width + xx] != 0)
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
            if (lowFrequencyDiff != nullptr && lowFrequencyDiff->size() == rawMask.size())
            {
                lowFrequencySum += (*lowFrequencyDiff)[index];
            }
            const size_t y = index / width;
            const size_t x = index % width;
            const size_t yBegin = (y == 0) ? 0 : y - 1;
            const size_t yEnd = std::min(height - 1, y + 1);
            const size_t xBegin = (x == 0) ? 0 : x - 1;
            const size_t xEnd = std::min(width - 1, x + 1);
            for (size_t yy = yBegin; yy <= yEnd; ++yy)
            {
                for (size_t xx = xBegin; xx <= xEnd; ++xx)
                {
                    const size_t next = yy * width + xx;
                    if (neighborMask[next] != 0 && visited[next] == 0)
                    {
                        visited[next] = 1;
                        stack.push_back(next);
                    }
                }
            }
        }

        maxComponent = std::max(maxComponent, componentPixels);
        if (componentPixels >= minComponentPixels)
        {
            const double lowFrequencyMean =
                (lowFrequencyDiff != nullptr && lowFrequencyDiff->size() == rawMask.size() && componentPixels > 0)
                    ? lowFrequencySum / static_cast<double>(componentPixels)
                    : std::numeric_limits<double>::infinity();
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

TactileFrameMetrics computeTactileFrameMetrics(const std::vector<uint8_t> &baseline,
                                               const std::vector<uint8_t> &current)
{
    TactileFrameMetrics metrics;
    if (baseline.size() != current.size() || baseline.empty())
    {
        metrics.damaged = true;
        metrics.robustResidualArea = 1.0;
        return metrics;
    }

    const auto baselineSorted = sortedFrameValues(baseline);
    const auto currentSorted = sortedFrameValues(current);
    const double baselineP5 = percentileFromSorted(baselineSorted, 0.05);
    const double baselineP50 = percentileFromSorted(baselineSorted, 0.50);
    const double baselineP95 = percentileFromSorted(baselineSorted, 0.95);
    const double currentP5 = percentileFromSorted(currentSorted, 0.05);
    const double currentP50 = percentileFromSorted(currentSorted, 0.50);
    const double currentP95 = percentileFromSorted(currentSorted, 0.95);
    const double currentSpan = std::max(1e-6, currentP95 - currentP5);
    metrics.gain = (baselineP95 - baselineP5) / currentSpan;
    metrics.offset = baselineP50 - metrics.gain * currentP50;

    std::vector<double> residuals;
    residuals.reserve(baseline.size());
    std::vector<double> baselineValues;
    baselineValues.reserve(baseline.size());
    std::vector<double> correctedCurrentValues;
    correctedCurrentValues.reserve(current.size());
    double residualSum = 0.0;
    for (size_t index = 0; index < baseline.size(); ++index)
    {
        const double baseValue = static_cast<double>(baseline[index]);
        const double currentValue = static_cast<double>(current[index]);
        const double correctedCurrent = std::clamp(currentValue * metrics.gain + metrics.offset, 0.0, 255.0);
        const double residual = std::fabs(correctedCurrent - baseValue);
        baselineValues.push_back(baseValue);
        correctedCurrentValues.push_back(correctedCurrent);
        residuals.push_back(residual);
        residualSum += residual;
    }

    metrics.residualMean = residualSum / static_cast<double>(baseline.size());
    std::vector<double> sortedResiduals = residuals;
    std::sort(sortedResiduals.begin(), sortedResiduals.end());
    metrics.residualMedian = percentileFromSorted(sortedResiduals, 0.50);
    metrics.residualP99 = percentileFromSorted(sortedResiduals, 0.99);

    std::vector<double> absoluteMedianDeviations;
    absoluteMedianDeviations.reserve(residuals.size());
    for (const double residual : residuals)
    {
        absoluteMedianDeviations.push_back(std::fabs(residual - metrics.residualMedian));
    }
    std::sort(absoluteMedianDeviations.begin(), absoluteMedianDeviations.end());
    metrics.residualMad = percentileFromSorted(absoluteMedianDeviations, 0.50);
    metrics.residualThreshold = std::max(
        kTactileResidualFloorThreshold,
        metrics.residualMedian + kTactileResidualMadMultiplier * kTactileMadToSigma * metrics.residualMad);

    std::vector<uint8_t> rawResidualMask;
    rawResidualMask.reserve(residuals.size());
    size_t rawResidualMaskCount = 0;
    for (size_t index = 0; index < residuals.size(); ++index)
    {
        if (residuals[index] >= metrics.residualThreshold)
        {
            rawResidualMask.push_back(1);
            ++rawResidualMaskCount;
        }
        else
        {
            rawResidualMask.push_back(0);
        }
    }
    metrics.rawResidualArea = static_cast<double>(rawResidualMaskCount) / static_cast<double>(baseline.size());
    const std::vector<double> baselineLowFrequency = boxBlurFrame(
        baselineValues,
        static_cast<size_t>(kTactileFrameWidth),
        static_cast<size_t>(kTactileFrameHeight),
        kTactileResidualLowFrequencyBlurRadius);
    const std::vector<double> currentLowFrequency = boxBlurFrame(
        correctedCurrentValues,
        static_cast<size_t>(kTactileFrameWidth),
        static_cast<size_t>(kTactileFrameHeight),
        kTactileResidualLowFrequencyBlurRadius);
    std::vector<double> lowFrequencyDiff;
    lowFrequencyDiff.reserve(baseline.size());
    for (size_t index = 0; index < baseline.size(); ++index)
    {
        lowFrequencyDiff.push_back(std::fabs(currentLowFrequency[index] - baselineLowFrequency[index]));
    }
    const size_t filteredResidualMaskCount = countFilteredResidualMask(
        rawResidualMask,
        static_cast<size_t>(kTactileFrameWidth),
        static_cast<size_t>(kTactileFrameHeight),
        kTactileResidualMinComponentPixels,
        &lowFrequencyDiff,
        &metrics.maxResidualComponentPixels,
        &metrics.droppedArtifactComponentPixels,
        &metrics.droppedArtifactComponentCount);
    metrics.robustResidualArea = static_cast<double>(filteredResidualMaskCount) / static_cast<double>(baseline.size());
    metrics.damaged = metrics.robustResidualArea >= kTactileRobustResidualAreaThreshold;
    return metrics;
}

std::string formatSeconds(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

bool parseDoubleStrict(const std::string &text, double *value)
{
    if (value == nullptr)
    {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno != 0 || !std::isfinite(parsed))
    {
        return false;
    }

    *value = parsed;
    return true;
}

bool extractJsonNumberField(const std::string &jsonText, const std::string &key, double *value)
{
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?(?:\\d+(?:\\.\\d+)?|\\.\\d+)(?:[eE][+-]?\\d+)?)");
    std::smatch match;
    if (!std::regex_search(jsonText, match, pattern) || match.size() < 2)
    {
        return false;
    }
    return parseDoubleStrict(match[1].str(), value);
}

bool extractJsonIntegerField(const std::string &jsonText, const std::string &key, int64_t *value)
{
    if (value == nullptr)
    {
        return false;
    }

    const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch match;
    if (!std::regex_search(jsonText, match, pattern) || match.size() < 2)
    {
        return false;
    }

    const std::string matched = match[1].str();
    char *end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(matched.c_str(), &end, 10);
    if (end == matched.c_str() || *end != '\0' || errno != 0)
    {
        return false;
    }

    *value = static_cast<int64_t>(parsed);
    return true;
}

CommandCaptureResult runCommandCapture(const std::vector<std::string> &arguments, int timeoutMs)
{
    CommandCaptureResult result;
    if (arguments.empty())
    {
        return result;
    }

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0)
    {
        return result;
    }

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments)
    {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const int childFdLimit = ChildFileDescriptorLimit();
    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0)
    {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        CloseChildFileDescriptors(childFdLimit);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);

    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true)
    {
        const pid_t waitResult = waitpid(pid, &status, WNOHANG);
        if (waitResult == pid)
        {
            break;
        }
        if (waitResult < 0)
        {
            close(pipefd[0]);
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            result.timedOut = true;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::array<char, 512> buffer{};
    ssize_t count = 0;
    while ((count = read(pipefd[0], buffer.data(), buffer.size())) > 0)
    {
        result.output.append(buffer.data(), static_cast<size_t>(count));
    }
    close(pipefd[0]);

    if (result.timedOut)
    {
        return result;
    }

    if (WIFEXITED(status))
    {
        result.exitCode = WEXITSTATUS(status);
        result.success = (result.exitCode == 0);
    }
    return result;
}

std::string shellSeconds(double seconds)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << seconds;
    return stream.str();
}

std::optional<std::vector<uint8_t>> captureTactileGrayFrame(const std::vector<std::string> &inputArguments,
                                                            double seekSeconds,
                                                            std::string *errorMessage)
{
    if (!commandExists("ffmpeg"))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ffmpeg not found in PATH";
        }
        return std::nullopt;
    }

    std::vector<std::string> arguments = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
    };
    arguments.insert(arguments.end(), inputArguments.begin(), inputArguments.end());
    if (seekSeconds > 0.0)
    {
        arguments.push_back("-ss");
        arguments.push_back(shellSeconds(seekSeconds));
    }
    arguments.push_back("-frames:v");
    arguments.push_back("1");
    arguments.push_back("-vf");
    arguments.push_back("crop=iw*0.82:ih:iw*0.18:0,scale=160:120,format=gray");
    arguments.push_back("-f");
    arguments.push_back("rawvideo");
    arguments.push_back("-");

    const CommandCaptureResult capture = runCommandCapture(arguments, kTactileSnapshotTimeoutMs);
    if (capture.timedOut)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ffmpeg timed out";
        }
        return std::nullopt;
    }
    if (!capture.success)
    {
        if (errorMessage != nullptr)
        {
            std::string detail = capture.output;
            detail.erase(std::remove(detail.begin(), detail.end(), '\r'), detail.end());
            while (!detail.empty() &&
                   (detail.back() == '\n' || detail.back() == ' ' || detail.back() == '\t'))
            {
                detail.pop_back();
            }
            *errorMessage = detail.empty() ? "ffmpeg failed" : detail;
        }
        return std::nullopt;
    }

    std::vector<uint8_t> bytes(capture.output.begin(), capture.output.end());
    if (bytes.size() != kTactileFrameBytes)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "unexpected frame bytes=" + std::to_string(bytes.size());
        }
        return std::nullopt;
    }
    return bytes;
}

bool probeVideoFile(const std::string &filePath, VideoProbeResult *result, std::string *errorMessage)
{
    if (result == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "internal error: missing probe output slot";
        }
        return false;
    }

    const int64_t probeStartMs = steadyNowMs();
    logPerf((::DA::utils::LogString() << "[PERF] ffprobe begin: file=" << filePath).str());
    const CommandCaptureResult probe = runCommandCapture(
        {
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=codec_type:format=start_time,duration",
            "-of",
            "default=noprint_wrappers=1:nokey=1",
            filePath,
        },
        kVideoProbeTimeoutMs);

    if (probe.timedOut)
    {
        logPerf((::DA::utils::LogString() << "[PERF] ffprobe timeout: file=" << filePath
                  << " elapsed_ms=" << (steadyNowMs() - probeStartMs)).str());
        if (errorMessage != nullptr)
        {
            *errorMessage = "ffprobe timeout after " + std::to_string(kVideoProbeTimeoutMs) + "ms";
        }
        return false;
    }
    if (!probe.success)
    {
        logPerf((::DA::utils::LogString() << "[PERF] ffprobe failed: file=" << filePath
                  << " elapsed_ms=" << (steadyNowMs() - probeStartMs)
                  << " exit_code=" << probe.exitCode).str());
        if (errorMessage != nullptr)
        {
            std::string detail = trim(probe.output);
            if (detail.empty())
            {
                detail = "ffprobe exited with code " + std::to_string(probe.exitCode);
            }
            *errorMessage = detail;
        }
        return false;
    }

    std::vector<std::string> lines;
    std::stringstream stream(probe.output);
    std::string line;
    while (std::getline(stream, line))
    {
        line = trim(line);
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }

    if (lines.size() < 3 || lines[0] != "video")
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ffprobe did not report a readable video stream";
        }
        return false;
    }

    if (lines[1] == "N/A" || lines[2] == "N/A" ||
        !parseDoubleStrict(lines[1], &result->startTimeSec) ||
        !parseDoubleStrict(lines[2], &result->durationSec))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ffprobe returned invalid start_time/duration";
        }
        return false;
    }

    result->spanSec = result->durationSec;
    if (result->startTimeSec > 0.0 && result->durationSec > result->startTimeSec)
    {
        result->spanSec = result->durationSec - result->startTimeSec;
    }

    if (!std::isfinite(result->spanSec) || result->spanSec <= 0.0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "ffprobe returned non-positive video span";
        }
        return false;
    }

    logPerf((::DA::utils::LogString() << "[PERF] ffprobe done: file=" << filePath
              << " elapsed_ms=" << (steadyNowMs() - probeStartMs)
              << " start_sec=" << formatSeconds(result->startTimeSec)
              << " duration_sec=" << formatSeconds(result->durationSec)
              << " span_sec=" << formatSeconds(result->spanSec)).str());
    return true;
}

std::vector<VideoProbeTaskResult> probeVideoFilesParallel(
    const std::string &episodeDir,
    const std::vector<EpisodeVideoArtifact> &artifacts)
{
    std::vector<std::future<VideoProbeTaskResult>> futures;
    futures.reserve(artifacts.size());
    for (const auto &artifact : artifacts)
    {
        futures.push_back(std::async(
            std::launch::async,
            [episodeDir, artifact]() {
                VideoProbeTaskResult task;
                task.probe.cameraName = artifact.cameraName;
                task.probe.fileName = artifact.fileName;
                task.ok = probeVideoFile(
                    (fs::path(episodeDir) / artifact.fileName).string(),
                    &task.probe,
                    &task.errorMessage);
                return task;
            }));
    }

    std::vector<VideoProbeTaskResult> results;
    results.reserve(futures.size());
    for (auto &future : futures)
    {
        results.push_back(future.get());
    }
    return results;
}

bool writeVideoProbeCacheToTimingFile(const std::string &episodeDir,
                                      const std::vector<VideoProbeResult> &probes,
                                      std::string *errorMessage)
{
    const fs::path timingPath = episodeTimingPath(episodeDir);
    json timing = json::object();
    if (!loadJsonFile(timingPath.string(), &timing, errorMessage))
    {
        return false;
    }
    if (!timing.is_object())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "internal timing file top-level value must be an object";
        }
        return false;
    }

    json cache = json::array();
    for (const auto &probe : probes)
    {
        cache.push_back(json::object({
            {"camera_name", probe.cameraName},
            {"file_name", probe.fileName},
            {"start_time_sec", probe.startTimeSec},
            {"duration_sec", probe.durationSec},
            {"span_sec", probe.spanSec},
        }));
    }
    timing["video_probes"] = std::move(cache);
    return writeTextFileAtomically(timingPath, timing.dump(2) + "\n", errorMessage);
}

bool loadCachedVideoProbe(const json &timing,
                          const EpisodeVideoArtifact &artifact,
                          VideoProbeResult *probe)
{
    if (probe == nullptr || !timing.contains("video_probes") || !timing["video_probes"].is_array())
    {
        return false;
    }

    for (const auto &entry : timing["video_probes"])
    {
        if (!entry.is_object() ||
            entry.value("camera_name", std::string()) != artifact.cameraName ||
            entry.value("file_name", std::string()) != artifact.fileName)
        {
            continue;
        }
        if (!entry.contains("start_time_sec") ||
            !entry.contains("duration_sec") ||
            !entry.contains("span_sec"))
        {
            return false;
        }
        const json &start = entry["start_time_sec"];
        const json &duration = entry["duration_sec"];
        const json &span = entry["span_sec"];
        if (!start.is_number() || !duration.is_number() || !span.is_number())
        {
            return false;
        }

        probe->cameraName = artifact.cameraName;
        probe->fileName = artifact.fileName;
        probe->startTimeSec = start.get<double>();
        probe->durationSec = duration.get<double>();
        probe->spanSec = span.get<double>();
        return std::isfinite(probe->durationSec) && probe->durationSec > 0.0 &&
               std::isfinite(probe->spanSec) && probe->spanSec > 0.0;
    }
    return false;
}

bool validateMetadataVideoDetails(const fs::path &metadataPath,
                                  const std::vector<EpisodeVideoArtifact> &artifacts,
                                  std::string *errorMessage)
{
    json metadata = json::object();
    if (!loadJsonFile(metadataPath.string(), &metadata, errorMessage))
    {
        return false;
    }
    if (!metadata.is_object())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metadata.json top-level value must be an object";
        }
        return false;
    }
    if (!metadata.contains("video_details") || !metadata["video_details"].is_array())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metadata.json missing video_details array";
        }
        return false;
    }

    std::set<std::string> requiredFileNames;
    for (const auto &artifact : artifacts)
    {
        requiredFileNames.insert(artifact.fileName);
    }

    std::set<std::string> seenFileNames;
    for (const auto &detail : metadata["video_details"])
    {
        if (!detail.is_object() ||
            !detail.contains("name") || !detail["name"].is_string() ||
            !detail.contains("start_offset_us") || !detail["start_offset_us"].is_number_integer() ||
            !detail.contains("duration_s") || !detail["duration_s"].is_number())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "metadata.json has invalid video_details entry";
            }
            return false;
        }

        const std::string fileName = detail["name"].get<std::string>();
        if (requiredFileNames.find(fileName) == requiredFileNames.end())
        {
            continue;
        }

        const int64_t startOffsetUs = detail["start_offset_us"].get<int64_t>();
        if (startOffsetUs < 0)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "metadata.json has negative start_offset_us for " + fileName;
            }
            return false;
        }

        const double durationS = detail["duration_s"].get<double>();
        if (!std::isfinite(durationS) || durationS <= 0.0)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "metadata.json has invalid duration_s for " + fileName;
            }
            return false;
        }
        seenFileNames.insert(fileName);
    }

    for (const auto &fileName : requiredFileNames)
    {
        if (seenFileNames.find(fileName) == seenFileNames.end())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "metadata.json missing video_details entry for " + fileName;
            }
            return false;
        }
    }
    return true;
}

bool loadLastTopicLogTimeFromTailChunks(const std::string &mcapPath,
                                        const std::string &topic,
                                        uint64_t *lastLogTimeNs,
                                        uint64_t *messageCount,
                                        std::string *errorMessage)
{
    if (lastLogTimeNs == nullptr || messageCount == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "internal error: missing encoder tail result slot";
        }
        return false;
    }

    *lastLogTimeNs = 0;
    *messageCount = 0;

    mcap::McapReader reader;
    const auto openStatus = reader.open(mcapPath);
    if (!openStatus.ok())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to open mcap: " + openStatus.message;
        }
        return false;
    }

    std::string summaryProblem;
    const auto summaryStatus = reader.readSummary(
        mcap::ReadSummaryMethod::NoFallbackScan,
        [&summaryProblem](const mcap::Status &status) {
            if (summaryProblem.empty())
            {
                summaryProblem = status.message;
            }
        });
    if (!summaryStatus.ok())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to read mcap summary: " + summaryStatus.message;
            if (!summaryProblem.empty())
            {
                *errorMessage += " (" + summaryProblem + ")";
            }
        }
        return false;
    }

    std::vector<mcap::ChannelId> matchingChannels;
    for (const auto &[channelId, channel] : reader.channels())
    {
        if (channel != nullptr && channel->topic == topic)
        {
            matchingChannels.push_back(channelId);
        }
    }

    if (matchingChannels.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "missing topic in mcap: " + topic;
        }
        return false;
    }

    if (reader.statistics().has_value())
    {
        for (const auto channelId : matchingChannels)
        {
            const auto countIt = reader.statistics()->channelMessageCounts.find(channelId);
            if (countIt != reader.statistics()->channelMessageCounts.end())
            {
                *messageCount += countIt->second;
            }
        }
    }

    bool found = false;
    auto *dataSource = reader.dataSource();
    const auto &chunkIndexes = reader.chunkIndexes();
    if (dataSource != nullptr && !chunkIndexes.empty())
    {
        size_t scannedChunks = 0;
        for (auto it = chunkIndexes.rbegin();
             it != chunkIndexes.rend() && scannedChunks < kEncoderTailChunkScanLimit && !found;
             ++it, ++scannedChunks)
        {
            mcap::TypedRecordReader recordReader(
                *dataSource,
                it->chunkStartOffset,
                it->chunkStartOffset + it->chunkLength);
            recordReader.onMessage = [&](const mcap::Message &message,
                                         mcap::ByteOffset,
                                         std::optional<mcap::ByteOffset>) {
                if (std::find(matchingChannels.begin(), matchingChannels.end(), message.channelId) ==
                    matchingChannels.end())
                {
                    return;
                }

                if (!found || message.logTime > *lastLogTimeNs)
                {
                    *lastLogTimeNs = message.logTime;
                    found = true;
                }
            };

            while (recordReader.next())
            {
            }

            if (!recordReader.status().ok() && errorMessage != nullptr && errorMessage->empty())
            {
                *errorMessage = "failed to read tail chunk: " + recordReader.status().message;
            }
        }
    }

    if (!found)
    {
        std::string readProblem;
        mcap::ReadMessageOptions options;
        options.topicFilter = [&topic](std::string_view candidateTopic) {
            return candidateTopic == topic;
        };

        for (const auto &messageView : reader.readMessages(
                 [&readProblem](const mcap::Status &status) {
                     if (readProblem.empty())
                     {
                         readProblem = status.message;
                     }
                 },
                 options))
        {
            if (!found || messageView.message.logTime > *lastLogTimeNs)
            {
                *lastLogTimeNs = messageView.message.logTime;
                found = true;
            }
        }

        if (!found && !readProblem.empty() && errorMessage != nullptr && errorMessage->empty())
        {
            *errorMessage = "failed to scan mcap messages: " + readProblem;
        }
    }

    if (!found)
    {
        if (errorMessage != nullptr && errorMessage->empty())
        {
            *errorMessage = "topic has zero messages: " + topic;
        }
        return false;
    }

    if (*messageCount == 0)
    {
        *messageCount = 1;
    }
    return true;
}

bool loadFaysMcapSummary(const std::string &mcapPath,
                         const std::string &imuTopic,
                         const std::string &cameraTopic,
                         FaysMcapSummary *summary,
                         std::string *errorMessage)
{
    (void)imuTopic;
    (void)cameraTopic;
    if (summary == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "internal error: missing Fays tail result slot";
        }
        return false;
    }

    *summary = FaysMcapSummary{};

    mcap::McapReader reader;
    const auto openStatus = reader.open(mcapPath);
    if (!openStatus.ok())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to open mcap: " + openStatus.message;
        }
        return false;
    }

    std::string summaryProblem;
    const auto summaryStatus = reader.readSummary(
        mcap::ReadSummaryMethod::NoFallbackScan,
        [&summaryProblem](const mcap::Status &status) {
            if (summaryProblem.empty())
            {
                summaryProblem = status.message;
            }
        });
    if (!summaryStatus.ok())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to read mcap summary: " + summaryStatus.message;
            if (!summaryProblem.empty())
            {
                *errorMessage += " (" + summaryProblem + ")";
            }
        }
        return false;
    }

    if (reader.statistics().has_value())
    {
        const auto imuCountIt = reader.statistics()->channelMessageCounts.find(kFaysImuChannelId);
        if (imuCountIt != reader.statistics()->channelMessageCounts.end())
        {
            summary->imuMessageCount = imuCountIt->second;
        }
        const auto cameraCountIt = reader.statistics()->channelMessageCounts.find(kFaysCameraChannelId);
        if (cameraCountIt != reader.statistics()->channelMessageCounts.end())
        {
            summary->cameraMessageCount = cameraCountIt->second;
        }
        const auto messageStartTime = reader.statistics()->messageStartTime;
        const auto messageEndTime = reader.statistics()->messageEndTime;
        if (messageEndTime > messageStartTime)
        {
            summary->spanNs = messageEndTime - messageStartTime;
        }
    }

    if (summary->imuMessageCount == 0 || summary->cameraMessageCount == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Fays summary has zero messages: imu_count=" +
                            std::to_string(summary->imuMessageCount) +
                            ", camera_count=" + std::to_string(summary->cameraMessageCount);
        }
        return false;
    }

    const auto classifyFaysMessage = [](const mcap::Message &message) {
        if (message.channelId == kFaysImuChannelId || message.dataSize == kFaysImuPayloadBytes)
        {
            return 'i';
        }
        if (message.channelId == kFaysCameraChannelId || message.dataSize == kFaysCameraPayloadBytes)
        {
            return 'c';
        }
        return '\0';
    };
    bool foundImu = false;
    bool foundFirstCamera = false;
    bool foundCamera = false;
    auto *dataSource = reader.dataSource();
    const auto &chunkIndexes = reader.chunkIndexes();
    if (dataSource != nullptr && !chunkIndexes.empty())
    {
        for (auto it = chunkIndexes.begin();
             it != chunkIndexes.end() && !foundFirstCamera;
             ++it)
        {
            mcap::TypedRecordReader recordReader(
                *dataSource,
                it->chunkStartOffset,
                it->chunkStartOffset + it->chunkLength);
            recordReader.onMessage = [&](const mcap::Message &message,
                                         mcap::ByteOffset,
                                         std::optional<mcap::ByteOffset>) {
                const char kind = classifyFaysMessage(message);
                if (kind == 'c' &&
                    (!foundFirstCamera || message.logTime < summary->firstCameraLogTimeNs))
                {
                    summary->firstCameraLogTimeNs = message.logTime;
                    summary->firstCameraPublishTimeNs = message.publishTime;
                    foundFirstCamera = true;
                }
            };

            while (recordReader.next())
            {
            }

            if (!recordReader.status().ok() && errorMessage != nullptr && errorMessage->empty())
            {
                *errorMessage = "failed to read Fays head chunk: " + recordReader.status().message;
            }
        }

        size_t scannedChunks = 0;
        for (auto it = chunkIndexes.rbegin();
             it != chunkIndexes.rend() && scannedChunks < kFaysTailChunkScanLimit && !(foundImu && foundCamera);
             ++it, ++scannedChunks)
        {
            mcap::TypedRecordReader recordReader(
                *dataSource,
                it->chunkStartOffset,
                it->chunkStartOffset + it->chunkLength);
            recordReader.onMessage = [&](const mcap::Message &message,
                                         mcap::ByteOffset,
                                         std::optional<mcap::ByteOffset>) {
                const char kind = classifyFaysMessage(message);
                if (kind == 'i')
                {
                    if (!foundImu || message.logTime > summary->lastImuLogTimeNs)
                    {
                        summary->lastImuLogTimeNs = message.logTime;
                        foundImu = true;
                    }
                }
                else if (kind == 'c')
                {
                    if (!foundCamera || message.logTime > summary->lastCameraLogTimeNs)
                    {
                        summary->lastCameraLogTimeNs = message.logTime;
                        summary->lastCameraPublishTimeNs = message.publishTime;
                        foundCamera = true;
                    }
                }
            };

            while (recordReader.next())
            {
            }

            if (!recordReader.status().ok() && errorMessage != nullptr && errorMessage->empty())
            {
                *errorMessage = "failed to read Fays tail chunk: " + recordReader.status().message;
            }
        }
    }
    else
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "missing Fays chunk indexes";
        }
        return false;
    }
    if (!foundImu || !foundCamera || !foundFirstCamera)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Fays topic has zero messages: imu=" +
                            std::string(foundImu ? "present" : "zero") +
                            ", camera=" +
                            std::string(foundCamera ? "present" : "zero") +
                            ", first_camera=" +
                            std::string(foundFirstCamera ? "present" : "zero");
        }
        return false;
    }
    if (summary->lastCameraLogTimeNs > summary->firstCameraLogTimeNs)
    {
        summary->cameraSpanNs = summary->lastCameraLogTimeNs - summary->firstCameraLogTimeNs;
    }
    if (summary->firstCameraPublishTimeNs == 0 ||
        summary->lastCameraPublishTimeNs <= summary->firstCameraPublishTimeNs)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Fays camera publishTime range is invalid: first_ns=" +
                            std::to_string(summary->firstCameraPublishTimeNs) +
                            ", last_ns=" + std::to_string(summary->lastCameraPublishTimeNs);
        }
        return false;
    }
    summary->cameraPublishSpanNs =
        summary->lastCameraPublishTimeNs - summary->firstCameraPublishTimeNs;
    if (summary->spanNs == 0 && !reader.chunkIndexes().empty())
    {
        uint64_t firstChunkMessageNs = std::numeric_limits<uint64_t>::max();
        uint64_t lastChunkMessageNs = 0;
        for (const auto &chunkIndex : reader.chunkIndexes())
        {
            if (chunkIndex.messageStartTime < firstChunkMessageNs)
            {
                firstChunkMessageNs = chunkIndex.messageStartTime;
            }
            if (chunkIndex.messageEndTime > lastChunkMessageNs)
            {
                lastChunkMessageNs = chunkIndex.messageEndTime;
            }
        }
        if (lastChunkMessageNs > firstChunkMessageNs && firstChunkMessageNs != std::numeric_limits<uint64_t>::max())
        {
            summary->spanNs = lastChunkMessageNs - firstChunkMessageNs;
        }
    }
    if (summary->cameraSpanNs < static_cast<uint64_t>(kFaysMcapMinSpanNs))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Fays camera span too short: camera_span_ns=" + std::to_string(summary->cameraSpanNs) +
                            ", threshold_ns=" + std::to_string(kFaysMcapMinSpanNs);
        }
        return false;
    }
    if (summary->spanNs < static_cast<uint64_t>(kFaysMcapMinSpanNs))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Fays mcap span too short: span_ns=" + std::to_string(summary->spanNs) +
                            ", threshold_ns=" + std::to_string(kFaysMcapMinSpanNs);
        }
        return false;
    }
    return true;
}

const FaysTailCheckTarget *faysTargetForStereoCamera(const std::string &cameraName)
{
    if (cameraName == "left_stereo")
    {
        return &kFaysTailCheckTargets[0];
    }
    if (cameraName == "right_stereo")
    {
        return &kFaysTailCheckTargets[1];
    }
    return nullptr;
}

bool loadEffectiveVideoDurationSec(const std::string &episodeDir,
                                   const EpisodeVideoArtifact &artifact,
                                   const VideoProbeResult &probe,
                                   const json *timingRoot,
                                   double fallbackSec,
                                   double *durationSec,
                                   std::string *source,
                                   std::string *errorMessage)
{
    if (durationSec == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "internal error: missing duration output slot";
        }
        return false;
    }

    *durationSec = fallbackSec;
    if (source != nullptr)
    {
        *source = "mkv_probe";
    }

    const FaysTailCheckTarget *target = faysTargetForStereoCamera(artifact.cameraName);
    if (target == nullptr)
    {
        return true;
    }

    const std::string cachedDurationKey =
        std::string(artifact.cameraName) + "_effective_duration_us";
    if (timingRoot != nullptr && timingRoot->is_object() &&
        timingRoot->contains(cachedDurationKey) &&
        (*timingRoot)[cachedDurationKey].is_number_unsigned())
    {
        const uint64_t cachedDurationUs = (*timingRoot)[cachedDurationKey].get<uint64_t>();
        if (cachedDurationUs > 0)
        {
            *durationSec = static_cast<double>(cachedDurationUs) / 1000000.0;
            if (source != nullptr)
            {
                *source = "fays_mcap_timing_cache";
            }
            return true;
        }
    }

    FaysMcapSummary summary;
    std::string faysError;
    if (!loadFaysMcapSummary(
            (fs::path(episodeDir) / target->mcapFileName).string(),
            target->imuTopic,
            target->cameraTopic,
            &summary,
            &faysError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to load stereo Fays camera duration for " +
                            std::string(artifact.fileName) + ": " + faysError;
        }
        return false;
    }

    *durationSec = static_cast<double>(summary.cameraPublishSpanNs) / 1000000000.0;
    if (source != nullptr)
    {
        *source = std::string("fays_mcap_camera:") + target->mcapFileName;
    }
    return true;
}

}  // namespace ugripper::runtime::record_runtime_detail

namespace
{
bool setEpisodeValidationFailure(std::string *errorMessage,
                                 std::vector<std::string> *errorTypes,
                                 const std::string &errorType,
                                 const std::string &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    if (errorTypes != nullptr && !errorType.empty())
    {
        if (std::find(errorTypes->begin(), errorTypes->end(), errorType) == errorTypes->end())
        {
            errorTypes->push_back(errorType);
        }
    }
    return false;
}
}

bool ugripper::runtime::EpisodeManager::validateEpisode(const std::string &episodeDir,
                                                    std::string *errorMessage,
                                                    std::vector<std::string> *errorTypes,
                                                    std::vector<TactileValidationFinding> *tactileFindings) const
{
    const int64_t validateStartMs = steadyNowMs();
    logPerf((::DA::utils::LogString() << "[PERF] validateEpisode begin: episode_dir=" << episodeDir).str());
    json timingRoot = json::object();
    std::string timingLoadError;
    loadJsonFile(episodeTimingPath(episodeDir).string(), &timingRoot, &timingLoadError);
    const auto artifacts = activeEpisodeVideoArtifacts(chestCameraEnabled_, stereoEnabled_);
    const std::vector<std::string> requiredFiles = {
        "sensor_left.mcap",
        "sensor_right.mcap",
        "calibration.json",
        "metadata.json",
    };

    for (const auto &artifact : artifacts)
    {
        const std::string path = episodeDir + "/" + artifact.fileName;
        if (!RecordRuntime::fileExistsAndNotEmpty(path))
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeMissingFile,
                "missing or empty video file: " + path);
        }
    }

    for (const auto &file : requiredFiles)
    {
        const std::string path = episodeDir + "/" + file;
        if (!RecordRuntime::fileExistsAndNotEmpty(path))
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeMissingFile,
                "missing or empty file: " + path);
        }
    }

    for (const auto &state : mainCameraRuntimeStates_)
    {
        if (!state.enabled)
        {
            continue;
        }
        if (!state.present)
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeCalibrationError,
                "main camera calibration cache invalid: camera=" + state.cameraName +
                    " device=" + state.devicePath + " missing");
        }
        if (!isYuzhouMainCameraSn(state.serialNumber))
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeCalibrationError,
                "main camera SN cache invalid: camera=" + state.cameraName +
                    " device=" + state.devicePath +
                    " detail=" + (state.lastError.empty() ? "empty SN" : state.lastError));
        }
        std::string calibrationDetail;
        if (!state.calibrationPayloadCached ||
            !validateYuzhouMainCameraCalibrationPayload(state.calibrationPayload, &calibrationDetail))
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeCalibrationError,
                "main camera calibration cache invalid: camera=" + state.cameraName +
                    " device=" + state.devicePath +
                    " detail=" + (!calibrationDetail.empty()
                                       ? calibrationDetail
                                       : (state.lastError.empty() ? "empty calibration" : state.lastError)));
        }
    }

    if (!commandExists("ffprobe"))
    {
        return setEpisodeValidationFailure(
            errorMessage,
            errorTypes,
            ugripper::runtime::kErrorTypeRuntimeError,
            "ffprobe is required for episode validation but was not found in PATH");
    }

    std::string metadataError;
    if (!validateMetadataVideoDetails(fs::path(episodeDir) / "metadata.json",
                                      artifacts,
                                      &metadataError))
    {
        return setEpisodeValidationFailure(
            errorMessage,
            errorTypes,
            ugripper::runtime::kErrorTypeMissingFile,
            "metadata validation failed: " + metadataError);
    }

    logPerf((::DA::utils::LogString() << "[PERF] validation setup done: episode_dir=" << episodeDir
             << " elapsed_ms=" << (steadyNowMs() - validateStartMs)).str());

    double referenceSpanSec = 0.0;
    const int64_t videoProbeStartMs = steadyNowMs();
    const auto probeTasks = probeVideoFilesParallel(episodeDir, artifacts);
    std::vector<VideoProbeResult> probes;
    std::map<std::string, double> effectiveSpanByFile;
    std::map<std::string, std::string> effectiveSpanSourceByFile;
    probes.reserve(probeTasks.size());
    for (const auto &task : probeTasks)
    {
        if (!task.ok)
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeMissingFile,
                "video unreadable or missing timing metadata: " + task.probe.fileName +
                    " (" + task.errorMessage + ")");
        }

        const EpisodeVideoArtifact artifact{task.probe.cameraName.c_str(), task.probe.fileName.c_str()};
        double effectiveSpanSec = task.probe.spanSec;
        std::string effectiveSpanSource;
        std::string effectiveSpanError;
        if (!loadEffectiveVideoDurationSec(
                episodeDir,
                artifact,
                task.probe,
                &timingRoot,
                task.probe.spanSec,
                &effectiveSpanSec,
                &effectiveSpanSource,
                &effectiveSpanError))
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeFrameLoss,
                effectiveSpanError);
        }

        if (effectiveSpanSec < kMinReasonableVideoSpanSec)
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeCollectionDurationTooShort,
                "video effective span too short: " + task.probe.fileName +
                    " span=" + formatSeconds(effectiveSpanSec) +
                    "s, source=" + effectiveSpanSource +
                    ", expected >=" + formatSeconds(kMinReasonableVideoSpanSec) + "s");
        }

        referenceSpanSec = std::max(referenceSpanSec, effectiveSpanSec);
        effectiveSpanByFile[task.probe.fileName] = effectiveSpanSec;
        effectiveSpanSourceByFile[task.probe.fileName] = effectiveSpanSource;
        probes.push_back(task.probe);
    }
    logPerf((::DA::utils::LogString() << "[PERF] parallel video probe done: episode_dir=" << episodeDir
                  << " count=" << probes.size()
                  << " elapsed_ms=" << (steadyNowMs() - videoProbeStartMs)).str());

    std::string probeCacheError;
    if (!writeVideoProbeCacheToTimingFile(episodeDir, probes, &probeCacheError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to cache video probe results in shm timing file: "
                         << probeCacheError << std::endl).str());
    }

    for (const auto &probe : probes)
    {
        const auto effectiveIt = effectiveSpanByFile.find(probe.fileName);
        const double effectiveSpanSec =
            effectiveIt != effectiveSpanByFile.end() ? effectiveIt->second : probe.spanSec;
        const auto sourceIt = effectiveSpanSourceByFile.find(probe.fileName);
        const std::string effectiveSpanSource =
            sourceIt != effectiveSpanSourceByFile.end() ? sourceIt->second : "mkv_probe";
        const double gapSec = referenceSpanSec - effectiveSpanSec;
        if (gapSec > kMaxVideoSpanGapSec)
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeFrameLoss,
                "video span gap too large: " + probe.fileName +
                    " span=" + formatSeconds(effectiveSpanSec) +
                    "s, source=" + effectiveSpanSource +
                    ", reference=" + formatSeconds(referenceSpanSec) +
                    "s, gap=" + formatSeconds(gapSec) + "s");
        }
    }

    const int64_t tailCheckStartMs = steadyNowMs();
    std::vector<std::future<TailCheckTaskResult>> tailCheckFutures;
    tailCheckFutures.reserve(
        kEncoderTailCheckTargets.size() + (stereoEnabled_ ? kFaysTailCheckTargets.size() : 0));

    for (const auto &target : kEncoderTailCheckTargets)
    {
        tailCheckFutures.push_back(std::async(
            std::launch::async,
            [episodeDir, target]() {
                TailCheckTaskResult result;
                uint64_t lastEncoderLogTimeNs = 0;
                uint64_t encoderMessageCount = 0;
                std::string encoderTailError;
                if (!loadLastTopicLogTimeFromTailChunks(
                        episodeDir + "/" + target.mcapFileName,
                        target.encoderTopic,
                        &lastEncoderLogTimeNs,
                        &encoderMessageCount,
                        &encoderTailError))
                {
                    result.errorMessage = std::string("encoder tail check failed for side=") + target.side +
                                          ": " + encoderTailError;
                    return result;
                }

                result.ok = true;
                result.detail = (::DA::utils::LogString()
                                 << "encoder_" << target.side
                                 << "{count=" << encoderMessageCount
                                 << ", last_ns=" << lastEncoderLogTimeNs
                                 << "}").str();
                return result;
            }));
    }

    if (stereoEnabled_)
    {
        for (const auto &target : kFaysTailCheckTargets)
        {
            tailCheckFutures.push_back(std::async(
                std::launch::async,
                [episodeDir, target]() {
                    TailCheckTaskResult result;
                    FaysMcapSummary faysSummary;
                    std::string faysTailError;
                    if (!loadFaysMcapSummary(
                            episodeDir + "/" + target.mcapFileName,
                            target.imuTopic,
                            target.cameraTopic,
                            &faysSummary,
                            &faysTailError))
                    {
                        result.errorMessage = std::string("Fays MCAP tail check failed for side=") + target.side +
                                              ": " + faysTailError;
                        return result;
                    }

                    int64_t lagNs = static_cast<int64_t>(faysSummary.lastCameraLogTimeNs) -
                                    static_cast<int64_t>(faysSummary.lastImuLogTimeNs);
                    if (lagNs < 0)
                    {
                        lagNs = 0;
                    }
                    if (lagNs > kFaysImuTailMaxLagNs)
                    {
                        result.errorMessage = std::string("Fays IMU tail lag too large for side=") + target.side +
                                              " (lag_ns=" + std::to_string(lagNs) +
                                              ", threshold_ns=" + std::to_string(kFaysImuTailMaxLagNs) +
                                              ", last_camera_ns=" + std::to_string(faysSummary.lastCameraLogTimeNs) +
                                              ", last_imu_ns=" + std::to_string(faysSummary.lastImuLogTimeNs) + ")";
                        return result;
                    }

                    result.ok = true;
                    result.detail = (::DA::utils::LogString()
                                     << "fays_" << target.side
                                     << "{imu_count=" << faysSummary.imuMessageCount
                                     << ", camera_count=" << faysSummary.cameraMessageCount
                                     << ", span_ms=" << (faysSummary.spanNs / 1000000.0)
                                     << ", camera_span_ms=" << (faysSummary.cameraSpanNs / 1000000.0)
                                     << ", camera_publish_span_ms=" << (faysSummary.cameraPublishSpanNs / 1000000.0)
                                     << ", first_camera_ns=" << faysSummary.firstCameraLogTimeNs
                                     << ", last_camera_ns=" << faysSummary.lastCameraLogTimeNs
                                     << ", last_imu_ns=" << faysSummary.lastImuLogTimeNs
                                     << ", lag_ms=" << (lagNs / 1000000.0)
                                     << "}").str();
                    return result;
                }));
        }
    }

    std::vector<std::string> tailDetails;
    tailDetails.reserve(tailCheckFutures.size());
    for (auto &future : tailCheckFutures)
    {
        TailCheckTaskResult result = future.get();
        if (!result.ok)
        {
            return setEpisodeValidationFailure(
                errorMessage,
                errorTypes,
                ugripper::runtime::kErrorTypeFrameLoss,
                result.errorMessage.empty() ? "tail check failed" : result.errorMessage);
        }
        tailDetails.push_back(result.detail);
    }
    logPerf((::DA::utils::LogString() << "[PERF] tail checks done: episode_dir=" << episodeDir
             << " count=" << tailDetails.size()
             << " elapsed_ms=" << (steadyNowMs() - tailCheckStartMs)
             << " details=" << joinStrings(tailDetails, "; ")).str());

    logPerf((::DA::utils::LogString() << "[PERF] validateEpisode end: episode_dir=" << episodeDir
              << " elapsed_ms=" << (steadyNowMs() - validateStartMs)
              << " reference_span_sec=" << formatSeconds(referenceSpanSec)).str());
    return true;
}

void ugripper::runtime::EpisodeManager::validateTactileEpisode(
    const std::string &episodeDir,
    std::vector<TactileValidationFinding> *tactileFindings) const
{
    const int64_t tactileValidationStartMs = steadyNowMs();
    const std::string bootId = currentBootId();
    json tactileHistory = json::object();
    {
        std::ifstream historyInput(tactileHistoryPath(tactileStateDir_));
        if (historyInput.is_open())
        {
            try
            {
                historyInput >> tactileHistory;
            }
            catch (const std::exception &ex)
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to parse tactile history, reset in-memory state: "
                                     << ex.what() << std::endl).str());
                tactileHistory = json::object();
            }
        }
    }
    if (!tactileHistory.is_object())
    {
        tactileHistory = json::object();
    }
    if (!tactileHistory.contains("cameras") || !tactileHistory["cameras"].is_object())
    {
        tactileHistory["cameras"] = json::object();
    }

    bool historyDirty = false;
    if (tactileFindings != nullptr)
    {
        tactileFindings->clear();
    }
    for (const auto &target : kTactileCalibrationTargets)
    {
        const std::string serial = cachedTactileSerialForName(target.cameraName);
        if (serial.empty())
        {
            continue;
        }

        std::string frameError;
        const char *fileName = episodeVideoFileNameForCamera(target.cameraName);
        if (fileName == nullptr || *fileName == '\0')
        {
            continue;
        }

        const auto currentFrame = captureTactileGrayFrame(
            {"-i", episodeDir + "/" + std::string(fileName)},
            kTactileEpisodeProbeSec,
            &frameError);
        if (!currentFrame.has_value())
        {
            const json &cameraHistory = tactileHistory["cameras"][serial];
            if (cameraHistory.is_object() &&
                (cameraHistory.value("reference_pending", false) ||
                 cameraHistory.value("persistent_baseline_pending", false) ||
                 cameraHistory.value("persistent_baseline_refresh_on_next_boot", false)))
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile reference deferred: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << frameError
                    << std::endl).str());
            }
            continue;
        }

        json &cameraHistory = tactileHistory["cameras"][serial];
        if (!cameraHistory.is_object())
        {
            cameraHistory = json::object();
        }
        if (!cameraHistory.contains("recent") || !cameraHistory["recent"].is_array())
        {
            cameraHistory["recent"] = json::array();
        }
        if (!cameraHistory.contains("persistent_recent") || !cameraHistory["persistent_recent"].is_array())
        {
            cameraHistory["persistent_recent"] = json::array();
        }
        cameraHistory["camera_name"] = target.cameraName;
        const uint64_t nowMs = RecordRuntime::currentEpochMs();
        cameraHistory["updated_at_ms"] = nowMs;

        std::vector<uint8_t> baselineFrame;
        std::string baselineError;
        const bool hasReferenceBaseline =
            readBinaryFileExact(tactileReferenceRawPath(tactileStateDir_, serial),
                                kTactileFrameBytes,
                                &baselineFrame,
                                &baselineError);
        const bool referencePending = cameraHistory.value("reference_pending", false);
        bool initializedReferenceFromEpisode = false;
        if (!hasReferenceBaseline || referencePending)
        {
            std::string writeError;
            if (writeBinaryFile(tactileReferenceRawPath(tactileStateDir_, serial),
                                *currentFrame,
                                &writeError) &&
                writeTactileReferenceMeta(tactileStateDir_,
                                          serial,
                                          target,
                                          RecordRuntime::currentEpochMs(),
                                          &writeError))
            {
                cameraHistory["reference_pending"] = false;
                cameraHistory["reference_pending_reason"] = "";
                cameraHistory["reference_pending_at_ms"] = 0;
                cameraHistory["reference_updated_at_ms"] = RecordRuntime::currentEpochMs();
                cameraHistory["recent"] = json::array();
                historyDirty = true;
                initializedReferenceFromEpisode = true;
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "tactile reference initialized from episode: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << (referencePending ? " reason=pending" : " reason=missing")
                    << std::endl).str());
            }
            else
            {
                cameraHistory["reference_pending"] = true;
                cameraHistory["reference_pending_reason"] =
                    referencePending ? cameraHistory.value("reference_pending_reason", std::string("write_failed"))
                                     : std::string("missing_reference_write_failed");
                historyDirty = true;
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile reference deferred: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << writeError
                    << std::endl).str());
            }
        }

        const bool persistentWarningActiveBefore =
            cameraHistory.value("persistent_fault_active", false);
        bool persistentWarningActiveAfter = persistentWarningActiveBefore;
        std::string persistentDetail =
            cameraHistory.value("persistent_fault_detail", std::string());

        bool persistentWarningTriggered = false;
        std::vector<uint8_t> persistentBaseline;
        std::string persistentBaselineError;
        const bool hasPersistentBaseline =
            readBinaryFileExact(tactilePersistentRawPath(tactileStateDir_, serial),
                                kTactileFrameBytes,
                                &persistentBaseline,
                                &persistentBaselineError);
        const uint64_t persistentBaselineUpdatedAtMs =
            cameraHistory.value("persistent_baseline_updated_at_ms", static_cast<uint64_t>(0));
        const bool legacyPersistentPending = cameraHistory.value("persistent_baseline_pending", false);
        const bool persistentRefreshOnNextBoot =
            cameraHistory.value("persistent_baseline_refresh_on_next_boot", false);
        const std::string persistentRefreshRequestBootId =
            cameraHistory.value("persistent_baseline_refresh_request_boot_id", std::string());
        const bool persistentRefreshAfterReboot =
            persistentRefreshOnNextBoot &&
            !persistentRefreshRequestBootId.empty() &&
            !bootId.empty() &&
            persistentRefreshRequestBootId != bootId;
        const bool persistentNeedsUpdate =
            !hasPersistentBaseline || persistentBaselineUpdatedAtMs == 0 ||
            persistentRefreshAfterReboot;
        const std::string persistentUpdateReason =
            (!hasPersistentBaseline || persistentBaselineUpdatedAtMs == 0)
                ? "missing"
                : (persistentRefreshAfterReboot ? "alarm_reboot" : "");
        if (legacyPersistentPending && !persistentNeedsUpdate)
        {
            cameraHistory["persistent_baseline_pending"] = false;
            cameraHistory["persistent_baseline_pending_reason"] = "";
            cameraHistory["persistent_baseline_pending_at_ms"] = 0;
            historyDirty = true;
        }
        bool initializedPersistentFromEpisode = false;
        if (persistentNeedsUpdate)
        {
            std::string writeError;
            if (writeBinaryFile(tactilePersistentRawPath(tactileStateDir_, serial),
                                *currentFrame,
                                &writeError))
            {
                cameraHistory["persistent_baseline_pending"] = false;
                cameraHistory["persistent_baseline_pending_reason"] = "";
                cameraHistory["persistent_baseline_pending_at_ms"] = 0;
                cameraHistory["persistent_baseline_updated_at_ms"] = nowMs;
                cameraHistory["persistent_baseline_refresh_on_next_boot"] = false;
                cameraHistory["persistent_baseline_refresh_request_boot_id"] = "";
                cameraHistory["persistent_baseline_refresh_request_at_ms"] = 0;
                cameraHistory["persistent_fault_active"] = false;
                cameraHistory["persistent_fault_detail"] = "";
                cameraHistory["persistent_recent"] = json::array();
                persistentWarningActiveAfter = false;
                persistentDetail.clear();
                historyDirty = true;
                initializedPersistentFromEpisode = true;
                DM_LOG_INFO("{}", (::DA::utils::LogString()
                    << "tactile persistent baseline initialized from episode: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << persistentUpdateReason
                    << std::endl).str());
            }
            else
            {
                cameraHistory["persistent_baseline_pending"] = true;
                cameraHistory["persistent_baseline_pending_reason"] = persistentUpdateReason + "_write_failed";
                cameraHistory["persistent_baseline_pending_at_ms"] = nowMs;
                historyDirty = true;
                DM_LOG_WARN("{}", (::DA::utils::LogString()
                    << "tactile persistent baseline deferred: camera=" << target.cameraName
                    << " serial=" << serial
                    << " episode=" << episodeDir
                    << " reason=" << writeError
                    << std::endl).str());
            }
        }

        if (!hasReferenceBaseline || referencePending || initializedReferenceFromEpisode)
        {
            continue;
        }

        const TactileFrameMetrics metrics = computeTactileFrameMetrics(baselineFrame, *currentFrame);
        bool warningActiveBefore = false;
        const json &recentBefore = cameraHistory["recent"];
        if (recentBefore.size() >= kTactileHistoryWindow)
        {
            warningActiveBefore = std::all_of(
                recentBefore.begin(),
                recentBefore.end(),
                [](const json &entry) { return entry.is_boolean() && entry.get<bool>(); });
        }

        json &recent = cameraHistory["recent"];
        recent.push_back(metrics.damaged);
        while (recent.size() > kTactileHistoryWindow)
        {
            recent.erase(recent.begin());
        }
        historyDirty = true;

        bool warningActive = false;
        if (recent.size() >= kTactileHistoryWindow)
        {
            warningActive = std::all_of(
                recent.begin(),
                recent.end(),
                [](const json &entry) { return entry.is_boolean() && entry.get<bool>(); });
        }

        if (hasPersistentBaseline && !persistentNeedsUpdate && !initializedPersistentFromEpisode)
        {
            const TactileFrameMetrics persistentMetrics =
                computeTactileFrameMetrics(persistentBaseline, *currentFrame);
            json &persistentRecent = cameraHistory["persistent_recent"];
            persistentRecent.push_back(persistentMetrics.damaged);
            while (persistentRecent.size() > kTactileHistoryWindow)
            {
                persistentRecent.erase(persistentRecent.begin());
            }

            bool persistentWindowActive = false;
            bool persistentWindowClean = false;
            if (persistentRecent.size() >= kTactileHistoryWindow)
            {
                persistentWindowActive = std::all_of(
                    persistentRecent.begin(),
                    persistentRecent.end(),
                    [](const json &entry) { return entry.is_boolean() && entry.get<bool>(); });
                persistentWindowClean = std::all_of(
                    persistentRecent.begin(),
                    persistentRecent.end(),
                    [](const json &entry) { return entry.is_boolean() && !entry.get<bool>(); });
            }

            if (persistentWindowActive)
            {
                persistentDetail = "persistent_record " + formatTactileMetrics(persistentMetrics);
                if (!persistentWarningActiveBefore)
                {
                    persistentWarningTriggered = true;
                }
                persistentWarningActiveAfter = true;
                cameraHistory["persistent_baseline_refresh_on_next_boot"] = !bootId.empty();
                cameraHistory["persistent_baseline_refresh_request_boot_id"] = bootId;
                cameraHistory["persistent_baseline_refresh_request_at_ms"] = nowMs;
            }
            else if (persistentWindowClean)
            {
                persistentWarningActiveAfter = false;
                persistentDetail.clear();
            }

            cameraHistory["persistent_fault_active"] = persistentWarningActiveAfter;
            cameraHistory["persistent_fault_detail"] = persistentDetail;
            historyDirty = true;
        }

        TactileValidationFinding finding;
        finding.cameraName = target.cameraName;
        finding.serialNumber = serial;
        finding.side = target.side;
        finding.sensorSlot = target.sensorSlot;
        finding.damaged = metrics.damaged;
        finding.warningActive = warningActive || persistentWarningActiveAfter;
        finding.warningTriggered =
            (warningActive && !warningActiveBefore) || persistentWarningTriggered;
        finding.audioCommand = std::string(target.cameraName) + "_damaged";
        finding.detail = formatTactileMetrics(metrics);
        if (!persistentDetail.empty())
        {
            finding.detail += " | " + persistentDetail;
        }
        if (tactileFindings != nullptr)
        {
            tactileFindings->push_back(std::move(finding));
        }
    }

    if (historyDirty)
    {
        std::string writeError;
        if (!writeTextFileAtomically(tactileHistoryPath(tactileStateDir_),
                                     tactileHistory.dump(2) + "\n",
                                     &writeError))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to persist tactile history: " << writeError << std::endl).str());
        }
    }
    if (tactileFindings != nullptr)
    {
        std::string metadataError;
        if (!updateTactileDamageMetadata(episodeDir, *tactileFindings, &metadataError))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "failed to update tactile damage metadata: episode=" << episodeDir
                << " error=" << metadataError << std::endl).str());
        }
    }
    logPerf((::DA::utils::LogString() << "[PERF] tactile validation done: episode_dir=" << episodeDir
             << " elapsed_ms=" << (steadyNowMs() - tactileValidationStartMs)).str());
}

bool ugripper::runtime::EpisodeManager::updateTactileDamageMetadata(
    const std::string &episodeDir,
    const std::vector<TactileValidationFinding> &tactileFindings,
    std::string *errorMessage) const
{
    const fs::path metadataPath = fs::path(episodeDir) / "metadata.json";
    ordered_json metadata;
    try
    {
        std::ifstream input(metadataPath);
        if (!input.is_open())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "cannot open metadata file: " + metadataPath.string();
            }
            return false;
        }
        input >> metadata;
    }
    catch (const std::exception &ex)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "failed to parse metadata file: " + std::string(ex.what());
        }
        return false;
    }

    if (!metadata.is_object() ||
        !metadata.contains("tactile_damage_sn_list") ||
        !metadata["tactile_damage_sn_list"].is_array())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "metadata tactile_damage_sn_list field is missing or invalid";
        }
        return false;
    }

    ordered_json damagedSerialNumbers = ordered_json::array();
    std::set<std::string> seenSerialNumbers;
    for (const auto &finding : tactileFindings)
    {
        if (finding.damaged &&
            !finding.serialNumber.empty() &&
            seenSerialNumbers.insert(finding.serialNumber).second)
        {
            damagedSerialNumbers.push_back(finding.serialNumber);
        }
    }
    metadata["tactile_damage_sn_list"] = std::move(damagedSerialNumbers);
    return writeTextFileAtomically(metadataPath, metadata.dump(2) + "\n", errorMessage);
}

const std::string &ugripper::runtime::EpisodeManager::dataRoot() const
{
    return episodeRoot_;
}
