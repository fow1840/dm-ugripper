#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace DA::Ugripper::detail {

bool moveFileWithCrossDeviceFallback(const std::filesystem::path &source,
                                     const std::filesystem::path &target,
                                     std::error_code *error);
bool flushFileToDisk(const std::filesystem::path &path, std::error_code *error);
void flushEpisodeArtifactsToDisk(const std::filesystem::path &episodeDir,
                                 bool chestCameraEnabled,
                                 bool stereoEnabled,
                                 const char *phaseLabel);
void writeValidationErrorLog(const std::filesystem::path &episodeDir,
                             const std::string &errorMessage);
std::string makeTimestampString();

}  // namespace DA::Ugripper::detail
