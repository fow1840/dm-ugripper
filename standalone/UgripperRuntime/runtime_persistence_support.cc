#include "record_runtime/runtime_persistence_support_internal.h"

#include "record_runtime/runtime_episode_internal.h"
#include "record_runtime/runtime_split_internal.h"
#include "record_runtime/runtime_support_internal.h"
#include "utils/ugripper_logger.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace DA::Ugripper::detail {
bool writeTextFileAtomically(const fs::path &path, const std::string &content, std::string *errorMessage)
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
    {
        std::ofstream output(tempPath, std::ios::trunc);
        if (!output.is_open())
        {
            const int savedErrno = errno;
            if (errorMessage != nullptr)
            {
                *errorMessage = "cannot open temp file for write: " + tempPath.string() +
                                (savedErrno != 0 ? " error=" + std::string(std::strerror(savedErrno)) : "");
            }
            return false;
        }
        output << content;
        output.flush();
        if (!output.good())
        {
            const int savedErrno = errno;
            if (errorMessage != nullptr)
            {
                *errorMessage = "failed to write temp file: " + tempPath.string() +
                                (savedErrno != 0 ? " error=" + std::string(std::strerror(savedErrno))
                                                 : " error=stream_write_failed");
            }
            output.close();
            fs::remove(tempPath, error);
            return false;
        }
    }

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

json *findJsonPath(json *root, const std::string &path, bool createMissing)
{
    if (root == nullptr || path.empty())
    {
        return nullptr;
    }

    json *cursor = root;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t dot = path.find('.', start);
        const std::string token =
            path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (token.empty())
        {
            return nullptr;
        }

        if (dot == std::string::npos)
        {
            if (!cursor->is_object())
            {
                return nullptr;
            }
            if (!cursor->contains(token))
            {
                if (!createMissing)
                {
                    return nullptr;
                }
                (*cursor)[token] = json::object();
            }
            return &(*cursor)[token];
        }

        if (!cursor->is_object())
        {
            return nullptr;
        }
        if (!cursor->contains(token))
        {
            if (!createMissing)
            {
                return nullptr;
            }
            (*cursor)[token] = json::object();
        }
        if (!(*cursor)[token].is_object())
        {
            if (!createMissing)
            {
                return nullptr;
            }
            (*cursor)[token] = json::object();
        }
        cursor = &(*cursor)[token];
        start = dot + 1;
    }

    return cursor;
}

const json *findJsonPathConst(const json *root, const std::string &path)
{
    if (root == nullptr || !root->is_object() || path.empty())
    {
        return nullptr;
    }

    const json *cursor = root;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t dot = path.find('.', start);
        const std::string token =
            path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (token.empty() || !cursor->is_object() || !cursor->contains(token))
        {
            return nullptr;
        }
        cursor = &(*cursor)[token];
        if (dot == std::string::npos)
        {
            return cursor;
        }
        start = dot + 1;
    }

    return nullptr;
}

bool loadJsonFile(const std::string &path, json *output, std::string *errorMessage)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "cannot open calibration source: " + path;
        }
        return false;
    }

    try
    {
        input >> *output;
        return true;
    }
    catch (const std::exception &error)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "invalid calibration json in " + path + ": " + error.what();
        }
        return false;
    }
}

bool loadCalibrationJsonFile(const std::string &path, json *output, std::string *errorMessage)
{
    if (!loadJsonFile(path, output, errorMessage))
    {
        return false;
    }

    if (!output->is_object())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "invalid calibration json in " + path + ": top-level JSON must be an object";
        }
        return false;
    }

    return true;
}

size_t gripperStateIndexForSide(const std::string &side)
{
    return side == "left" ? 0u : 1u;
}

const char *mainCameraJsonPathForSide(const std::string &side)
{
    return side == "left" ? "observation.images.cam_left_main"
                          : "observation.images.cam_right_main";
}

const char *stereoJsonPathForSide(const std::string &side)
{
    return side == "left" ? "observation.images.stereo_left"
                          : "observation.images.stereo_right";
}

const char *imuJsonPathForSide(const std::string &side)
{
    return side == "left" ? "observation.imu.imu_left"
                          : "observation.imu.imu_right";
}

std::string joinArguments(const std::vector<std::string> &arguments)
{
    std::ostringstream stream;
    for (size_t index = 0; index < arguments.size(); ++index)
    {
        if (index > 0)
        {
            stream << ' ';
        }
        stream << arguments[index];
    }
    return stream.str();
}

std::string joinStrings(const std::vector<std::string> &items, const char *separator)
{
    std::ostringstream stream;
    for (size_t index = 0; index < items.size(); ++index)
    {
        if (index > 0)
        {
            stream << separator;
        }
        stream << items[index];
    }
    return stream.str();
}

bool moveFileWithCrossDeviceFallback(const fs::path &source,
                                     const fs::path &target,
                                     std::error_code *error)
{
    std::error_code localError;
    fs::rename(source, target, localError);
    if (!localError)
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    if (localError != std::make_error_code(std::errc::cross_device_link))
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }

    localError.clear();
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, localError);
    if (localError)
    {
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }

    fs::remove(source, localError);
    if (localError)
    {
        std::error_code cleanupError;
        fs::remove(target, cleanupError);
        if (error != nullptr)
        {
            *error = localError;
        }
        return false;
    }

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

int64_t steadyNowMs()
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

bool flushFileToDisk(const fs::path &path, std::error_code *error)
{
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        if (error != nullptr)
        {
            *error = std::error_code(errno, std::generic_category());
        }
        return false;
    }

    if (::fsync(fd) != 0)
    {
        const std::error_code syncError(errno, std::generic_category());
        close(fd);
        if (error != nullptr)
        {
            *error = syncError;
        }
        return false;
    }

    close(fd);

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool flushDirectoryToDisk(const fs::path &path, std::error_code *error)
{
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (fd < 0)
    {
        if (error != nullptr)
        {
            *error = std::error_code(errno, std::generic_category());
        }
        return false;
    }

    if (::fsync(fd) != 0)
    {
        const std::error_code syncError(errno, std::generic_category());
        close(fd);
        if (error != nullptr)
        {
            *error = syncError;
        }
        return false;
    }

    close(fd);

    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

void flushEpisodeDirectoriesToDisk(const fs::path &episodeDir, const char *phaseLabel)
{
    if (episodeDir.empty())
    {
        return;
    }

    std::vector<fs::path> directories;
    directories.push_back(episodeDir);

    const fs::path parentDir = episodeDir.parent_path();
    if (!parentDir.empty() && parentDir != episodeDir)
    {
        directories.push_back(parentDir);
    }

    for (const auto &directory : directories)
    {
        if (!fs::exists(directory))
        {
            logPerf((::DA::utils::LogString() << "[PERF] flush dir skip missing: phase=" << phaseLabel
                      << " path=" << directory).str());
            continue;
        }

        const int64_t dirFlushStartMs = steadyNowMs();
        std::error_code error;
        if (!flushDirectoryToDisk(directory, &error))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush episode directory: phase=" << phaseLabel
                      << " path=" << directory
                      << " error=" << error.message() << std::endl).str());
            continue;
        }

        logPerf((::DA::utils::LogString() << "[PERF] flush dir done: phase=" << phaseLabel
                  << " path=" << directory
                  << " elapsed_ms=" << (steadyNowMs() - dirFlushStartMs)).str());
    }
}

void flushDirectoryTreeToDisk(const fs::path &rootDir, const char *phaseLabel)
{
    if (rootDir.empty())
    {
        return;
    }

    std::error_code rootStatusError;
    if (!fs::is_directory(rootDir, rootStatusError))
    {
        return;
    }

    std::vector<fs::path> directories;
    directories.push_back(rootDir);

    std::error_code walkError;
    for (fs::recursive_directory_iterator it(rootDir, fs::directory_options::skip_permission_denied, walkError), end;
         it != end;
         it.increment(walkError))
    {
        if (walkError)
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to walk directory tree for flush: phase="
                          << phaseLabel << " root=" << rootDir
                          << " error=" << walkError.message() << std::endl).str());
            walkError.clear();
            continue;
        }

        std::error_code statusError;
        if (it->is_directory(statusError))
        {
            directories.push_back(it->path());
        }
    }

    for (auto rit = directories.rbegin(); rit != directories.rend(); ++rit)
    {
        const int64_t dirFlushStartMs = steadyNowMs();
        std::error_code error;
        if (!flushDirectoryToDisk(*rit, &error))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush directory tree entry: phase="
                          << phaseLabel << " path=" << *rit
                          << " error=" << error.message() << std::endl).str());
            continue;
        }

        logPerf((::DA::utils::LogString() << "[PERF] flush dir tree done: phase=" << phaseLabel
                  << " path=" << *rit
                  << " elapsed_ms=" << (steadyNowMs() - dirFlushStartMs)).str());
    }
}

bool shouldFlushVideoArtifactForPhase(const EpisodeVideoArtifact &artifact, const std::string &phase)
{
    const bool isStereo = std::string(artifact.fileName).find("stereo_") == 0;
    if (phase == "pre_stereo_finalize")
    {
        return !isStereo;
    }
    if (phase == "final")
    {
        return isStereo;
    }
    return false;
}

bool shouldFlushPathForPhase(const fs::path &episodeDir,
                             const fs::path &path,
                             const std::string &phase,
                             const std::set<std::string> &selectedVideoFileNames)
{
    (void)episodeDir;
    const std::string fileName = path.filename().string();
    if (selectedVideoFileNames.find(fileName) != selectedVideoFileNames.end())
    {
        return true;
    }

    if (phase == "pre_stereo_finalize")
    {
        return fileName == "sensor_left.mcap" ||
               fileName == "sensor_right.mcap" ||
               fileName == "calibration.json" ||
               fileName == "audio_pre.wav" ||
               fileName == "audio_post.wav";
    }
    if (phase == "final" || phase == "ego_cleanup")
    {
        if (phase == "ego_cleanup")
        {
            const fs::path statusPath = episodeDir / "ego" / kEgoSyncStatusFileName;
            return path == statusPath;
        }
        const fs::path egoRelativePath = path.lexically_relative(episodeDir / "ego");
        if (!egoRelativePath.empty() &&
            egoRelativePath != fs::path(".") &&
            egoRelativePath.string().find("..") != 0)
        {
            return true;
        }
        return fileName == "fays_data_left.mcap" ||
               fileName == "fays_data_right.mcap";
    }
    if (phase == "metadata_final")
    {
        return fileName == "metadata.json" ||
               fileName == "validation_error.log";
    }
    return true;
}

void flushEpisodeArtifactsToDisk(const fs::path &episodeDir,
                                 bool chestCameraEnabled,
                                 bool stereoEnabled,
                                 const char *phaseLabel)
{
    if (episodeDir.empty())
    {
        return;
    }

    const std::string phase = phaseLabel != nullptr ? phaseLabel : "";
    std::vector<fs::path> paths;
    const auto artifacts = activeEpisodeVideoArtifacts(chestCameraEnabled, stereoEnabled);
    std::set<std::string> selectedVideoFileNames;
    paths.reserve(artifacts.size() + 9);

    for (const auto &artifact : artifacts)
    {
        if (shouldFlushVideoArtifactForPhase(artifact, phase))
        {
            paths.push_back(episodeDir / artifact.fileName);
            selectedVideoFileNames.insert(artifact.fileName);
        }
    }

    paths.push_back(episodeDir / "sensor_left.mcap");
    paths.push_back(episodeDir / "sensor_right.mcap");
    if (stereoEnabled)
    {
        paths.push_back(episodeDir / "fays_data_left.mcap");
        paths.push_back(episodeDir / "fays_data_right.mcap");
    }
    paths.push_back(episodeDir / "metadata.json");
    paths.push_back(episodeDir / "calibration.json");
    paths.push_back(episodeDir / "validation_error.log");
    paths.push_back(episodeDir / kEgoSyncStatusFileName);

    const fs::path egoDir = episodeDir / "ego";
    if ((phase == "final" || phase == "ego_cleanup") && fs::exists(egoDir))
    {
        std::error_code walkError;
        for (fs::recursive_directory_iterator it(egoDir, fs::directory_options::skip_permission_denied, walkError), end;
             it != end;
             it.increment(walkError))
        {
            if (walkError)
            {
                DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to walk ego artifacts for flush: "
                              << egoDir << " error=" << walkError.message() << std::endl).str());
                walkError.clear();
                continue;
            }

            std::error_code statusError;
            if (it->is_regular_file(statusError))
            {
                paths.push_back(it->path());
            }
        }
    }

    const fs::path audioPre = episodeDir / "audio_pre.wav";
    if (fs::exists(audioPre))
    {
        paths.push_back(audioPre);
    }

    const fs::path audioPost = episodeDir / "audio_post.wav";
    if (fs::exists(audioPost))
    {
        paths.push_back(audioPost);
    }

    const int64_t flushStartMs = steadyNowMs();
    std::vector<fs::path> filteredPaths;
    filteredPaths.reserve(paths.size());
    for (const auto &path : paths)
    {
        if (shouldFlushPathForPhase(episodeDir, path, phase, selectedVideoFileNames))
        {
            filteredPaths.push_back(path);
        }
    }

    DM_LOG_INFO("{}", (::DA::utils::LogString() << "[STOP_DIAG] flush begin: phase=" << phaseLabel
              << " episode_dir=" << episodeDir
              << " candidate_paths=" << filteredPaths.size()).str());

    for (const auto &path : filteredPaths)
    {
        if (!fs::exists(path))
        {
            logPerf((::DA::utils::LogString() << "[PERF] flush skip missing: phase=" << phaseLabel
                     << " path=" << path).str());
            continue;
        }

        const int64_t artifactFlushStartMs = steadyNowMs();
        std::error_code error;
        if (!flushFileToDisk(path, &error))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush episode artifact: "
                      << path << " error=" << error.message() << std::endl).str());
            continue;
        }

        if (steadyNowMs() - artifactFlushStartMs >= 1000)
        {
            DM_LOG_WARN("[STOP_DIAG] slow artifact flush: phase={} path={} elapsed_ms={}",
                        phase, path.string(), steadyNowMs() - artifactFlushStartMs);
        }

        logPerf((::DA::utils::LogString() << "[PERF] flush done: phase=" << phaseLabel
                  << " path=" << path
                  << " elapsed_ms=" << (steadyNowMs() - artifactFlushStartMs)).str());
    }

    if (phase == "final" || phase == "ego_cleanup")
    {
        flushDirectoryTreeToDisk(egoDir, phaseLabel);
    }
    flushEpisodeDirectoriesToDisk(episodeDir, phaseLabel);

    DM_LOG_INFO("{}", (::DA::utils::LogString() << "[STOP_DIAG] flush end: phase=" << phaseLabel
              << " episode_dir=" << episodeDir
              << " elapsed_ms=" << (steadyNowMs() - flushStartMs)).str());
}

std::string makeTimestampString()
{
    const auto now = std::chrono::system_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(nowMs);
}

std::string makeLocalDateTimeString()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&nowTime, &localTime);

    char timeBuffer[32] = {0};
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return std::string(timeBuffer);
}

void writeValidationErrorLog(const fs::path &episodeDir, const std::string &errorMessage)
{
    if (episodeDir.empty())
    {
        return;
    }

    const fs::path errorLogPath = episodeDir / "validation_error.log";
    const std::string details = errorMessage.empty() ? "unknown validation error" : errorMessage;
    const std::string content = "Validation failed at " + makeLocalDateTimeString() + ": " + details + "\n";

    std::string writeError;
    if (!writeTextFileAtomically(errorLogPath, content, &writeError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to write validation_error.log: " << writeError << std::endl).str());
        return;
    }

    std::error_code flushError;
    if (!flushFileToDisk(errorLogPath, &flushError))
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to flush validation_error.log: " << flushError.message() << std::endl).str());
    }
    flushEpisodeDirectoriesToDisk(episodeDir, "validation_error");
}

}  // namespace DA::Ugripper::detail
