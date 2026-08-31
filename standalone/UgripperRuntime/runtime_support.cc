#include "record_runtime.h"
#include "record_runtime/runtime_episode_internal.h"
#include "record_runtime/runtime_split_internal.h"
#include "record_runtime/runtime_support_internal.h"
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


namespace ugripper::runtime::record_runtime_detail {
int ChildFileDescriptorLimit()
{
    const long openMax = sysconf(_SC_OPEN_MAX);
    return openMax > 0 ? static_cast<int>(openMax) : 1024;
}

void CloseChildFileDescriptors(int fdLimit)
{
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3U, ~0U, 0U) == 0)
    {
        return;
    }
#endif
    for (int fd = 3; fd < fdLimit; ++fd)
    {
        close(fd);
    }
}

extern const uint64_t kSystemActionResultWaitMs = 8000;
extern const uint64_t kRestoreUsbSymlinkPollIntervalMs = 1000;
extern const uint64_t kRestoreUsbSymlinkMaxWaitMs = 25000;
extern const uint64_t kRestoreUsbReadyCheckDelayMs = 45000;
extern const uint64_t kRestoreUsbFailureAlarmDurationMs = 5000;
extern const int kBeepSilenceRetryLimit = 5;
extern const int kBeepSilenceConfirmMs = 150;
extern const uint64_t kRestoreUsbManualInsertGraceMs = 20000;
extern const uint64_t kRestoreUsbTriggerStableMs = 6000;
extern const uint64_t kRestoreUsbPreflightFailureCooldownMs = 30000;
extern const int kRestoreUsbMaxAttemptsPerError = 3;
extern const uint64_t kCameraRecorderDStateFaultMs = 5000;
extern const uint64_t kCameraRecorderDStateLogIntervalMs = 3000;
extern const uint64_t kGripperRefreshActiveTimeoutMs = 1500;
extern const uint64_t kMainCameraRefreshDelayMs = 1500;
extern const uint64_t kMainCameraRefreshRetryMs = 3000;
extern const uint64_t kTactileSerialRefreshRetryMs = 3000;
extern const char *const kMainCameraV4l2KernelHangKey = "main_camera_v4l2_kernel_hang";
extern const char *const kMainCameraV4l2StartupFailureKey = "main_camera_v4l2_startup_failure";
extern const char *const kMainCameraShortStreamKey = "main_camera_short_stream";

bool IsStereoStartupFaultKey(const std::string& key)
{
    return key == "stereo_daemon_not_running" ||
           key == "stereo_status_missing" ||
           key == "stereo_status_invalid" ||
           key == "stereo_not_ready" ||
           key == ugripper::runtime::kErrorTypeStereoControlFailed;
}

bool g_perfLogEnabled = kPerfLogEnabled;

bool perfLogEnabled()
{
    return g_perfLogEnabled;
}

std::string shellQuote(const std::string &value)
{
    std::string quoted = "'";
    for (const char ch : value)
    {
        if (ch == '\'')
        {
            quoted += "'\\''";
        }
        else
        {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

void logPerf(const std::string &message)
{
    if (!perfLogEnabled())
    {
        return;
    }
    DM_LOG_INFO("{}", (::DA::utils::LogString() << message << std::endl).str());
}

std::optional<ugripper::runtime::HealthFault> MakeDiskHealthFault(const std::string &key,
                                                                  const std::string &detail)
{
    return ugripper::runtime::HealthFault{
        .led_state = ugripper::runtime::RuntimeLedState::Error3,
        .side = ugripper::runtime::HardwareFaultSide::Unknown,
        .key = key,
        .detail = detail,
    };
}

bool ReadProcTaskState(const fs::path &statPath, char *state, std::string *comm)
{
    std::ifstream input(statPath);
    std::string line;
    if (!input.is_open() || !std::getline(input, line))
    {
        return false;
    }
    const size_t leftParen = line.find('(');
    const size_t rightParen = line.rfind(')');
    if (leftParen == std::string::npos ||
        rightParen == std::string::npos ||
        rightParen + 2 >= line.size())
    {
        return false;
    }
    if (state != nullptr)
    {
        *state = line[rightParen + 2];
    }
    if (comm != nullptr)
    {
        *comm = line.substr(leftParen + 1, rightParen - leftParen - 1);
    }
    return true;
}

std::string ReadSmallTextFileTrimmed(const fs::path &path)
{
    std::ifstream input(path);
    std::string text;
    if (!input.is_open() || !std::getline(input, text))
    {
        return std::string();
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.pop_back();
    }
    return text;
}

std::optional<ProcTaskDState> FindFirstDStateTask(int pid)
{
    if (pid <= 0)
    {
        return std::nullopt;
    }
    const fs::path taskRoot = fs::path("/proc") / std::to_string(pid) / "task";
    std::error_code error;
    if (!fs::exists(taskRoot, error) || !fs::is_directory(taskRoot, error))
    {
        return std::nullopt;
    }

    for (const auto &entry : fs::directory_iterator(taskRoot, error))
    {
        if (error)
        {
            break;
        }
        if (!entry.is_directory(error))
        {
            continue;
        }
        const std::string taskId = entry.path().filename().string();
        char state = '\0';
        std::string comm;
        if (!ReadProcTaskState(entry.path() / "stat", &state, &comm))
        {
            continue;
        }
        if (state != 'D')
        {
            continue;
        }
        return ProcTaskDState{
            taskId,
            comm,
            ReadSmallTextFileTrimmed(entry.path() / "wchan"),
        };
    }
    return std::nullopt;
}

bool IsCameraRecorderKernelHangReason(const std::string &reason)
{
    return reason.find(kMainCameraV4l2KernelHangKey) != std::string::npos;
}

bool IsCameraRecorderMainCameraFault(const ugripper::runtime::HealthFault &fault)
{
    return fault.key == kMainCameraV4l2KernelHangKey ||
           fault.key == kMainCameraV4l2StartupFailureKey ||
           fault.key == kMainCameraShortStreamKey;
}

bool IsDiskHealthFault(const ugripper::runtime::HealthFault &fault)
{
    return fault.key == "disk_mount_lost" ||
           fault.key == "disk_not_writable" ||
           fault.key == "disk_full" ||
           fault.key == "disk_space_unavailable" ||
           fault.led_state == ugripper::runtime::RuntimeLedState::Error3;
}

bool IsImmediateRestoreUsbTriggerCause(const std::string &cause)
{
    return cause == kMainCameraV4l2KernelHangKey ||
           cause == kMainCameraV4l2StartupFailureKey ||
           cause == kMainCameraShortStreamKey;
}

bool isMountedDiskRoot(const fs::path &diskRoot)
{
    std::ifstream mountInfo("/proc/self/mountinfo");
    if (!mountInfo.is_open())
    {
        return false;
    }

    std::string line;
    while (std::getline(mountInfo, line))
    {
        const size_t separator = line.find(" - ");
        const std::string leftPart = separator == std::string::npos ? line : line.substr(0, separator);
        std::istringstream fields(leftPart);
        std::string mountId;
        std::string parentId;
        std::string majorMinor;
        std::string root;
        std::string mountPoint;
        if (!(fields >> mountId >> parentId >> majorMinor >> root >> mountPoint))
        {
            continue;
        }
        if (mountPoint == diskRoot.string())
        {
            return true;
        }
    }
    return false;
}

std::optional<ugripper::runtime::HealthFault> detectDiskHealthFault(const std::string &diskRootPath)
{
    if (diskRootPath.empty())
    {
        return MakeDiskHealthFault("disk_mount_lost", "Disk root path is empty");
    }

    const fs::path diskRoot(diskRootPath);
    std::error_code pathError;
    if (!fs::exists(diskRoot, pathError) || !fs::is_directory(diskRoot, pathError))
    {
        const std::string suffix = pathError ? " error=" + pathError.message() : "";
        return MakeDiskHealthFault("disk_mount_lost",
                                   "Disk root unavailable: " + diskRootPath + suffix);
    }

    if (!isMountedDiskRoot(diskRoot))
    {
        return MakeDiskHealthFault("disk_mount_lost", "Disk root is not mounted: " + diskRootPath);
    }

    errno = 0;
    if (access(diskRoot.c_str(), W_OK) != 0)
    {
        const int savedErrno = errno;
        const std::string errorText = std::strerror(savedErrno);
        if (savedErrno == EROFS)
        {
            return MakeDiskHealthFault("disk_not_writable",
                                       "Disk root is read-only: " + diskRootPath + " error=" + errorText);
        }
        return MakeDiskHealthFault("disk_not_writable",
                                   "Disk root not writable: " + diskRootPath + " error=" + errorText);
    }

    std::error_code spaceError;
    const fs::space_info spaceInfo = fs::space(diskRoot, spaceError);
    if (spaceError)
    {
        return MakeDiskHealthFault("disk_space_unavailable",
                                   "Disk root space info failed: " + diskRootPath + " error=" + spaceError.message());
    }
    if (spaceInfo.available == 0)
    {
        return MakeDiskHealthFault(
            "disk_full",
            "Disk root has no available space: " + diskRootPath +
                " available_bytes=0 capacity_bytes=" + std::to_string(spaceInfo.capacity));
    }

    return std::nullopt;
}

constexpr std::array<const char *, 13> kAllCriticalDevicePaths = {{
    "/dev/cam_right",
    "/dev/cam_left",
    "/dev/cam_chest",
    "/dev/stereo_right",
    "/dev/stereo_left",
    "/dev/right_fays_imu",
    "/dev/left_fays_imu",
    "/dev/tcam_right_l",
    "/dev/tcam_right_r",
    "/dev/tcam_left_l",
    "/dev/tcam_left_r",
    "/dev/right_encoder",
    "/dev/left_encoder",
}};

extern const std::array<const char *, 6> kLeftCriticalDevicePaths = {{
    "/dev/cam_left",
    "/dev/stereo_left",
    "/dev/left_fays_imu",
    "/dev/tcam_left_l",
    "/dev/tcam_left_r",
    "/dev/left_encoder",
}};

extern const std::array<const char *, 6> kRightCriticalDevicePaths = {{
    "/dev/cam_right",
    "/dev/stereo_right",
    "/dev/right_fays_imu",
    "/dev/tcam_right_l",
    "/dev/tcam_right_r",
    "/dev/right_encoder",
}};

ugripper::runtime::HardwareFaultSide restoreUsbSideForText(const std::string &text)
{
    const bool hasLeft = text.find("left") != std::string::npos ||
                         text.find("/dev/cam_left") != std::string::npos ||
                         text.find("/dev/stereo_left") != std::string::npos ||
                         text.find("/dev/left_") != std::string::npos ||
                         text.find("/dev/tcam_left") != std::string::npos;
    const bool hasRight = text.find("right") != std::string::npos ||
                          text.find("/dev/cam_right") != std::string::npos ||
                          text.find("/dev/stereo_right") != std::string::npos ||
                          text.find("/dev/right_") != std::string::npos ||
                          text.find("/dev/tcam_right") != std::string::npos;
    if (hasLeft && hasRight)
    {
        return ugripper::runtime::HardwareFaultSide::Both;
    }
    if (hasLeft)
    {
        return ugripper::runtime::HardwareFaultSide::Left;
    }
    if (hasRight)
    {
        return ugripper::runtime::HardwareFaultSide::Right;
    }
    return ugripper::runtime::HardwareFaultSide::Unknown;
}

const char *restoreUsbSideName(ugripper::runtime::HardwareFaultSide side)
{
    switch (side)
    {
    case ugripper::runtime::HardwareFaultSide::Left:
        return "left";
    case ugripper::runtime::HardwareFaultSide::Right:
        return "right";
    case ugripper::runtime::HardwareFaultSide::Both:
        return "both";
    case ugripper::runtime::HardwareFaultSide::Unknown:
    default:
        return "unknown";
    }
}

std::string compactRestoreUsbEvidence(std::string detail)
{
    const std::string prefix = "Critical device nodes missing:";
    const size_t prefixPos = detail.find(prefix);
    if (prefixPos != std::string::npos)
    {
        detail = detail.substr(prefixPos + prefix.size());
    }
    detail.erase(detail.begin(), std::find_if(detail.begin(), detail.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    detail.erase(std::find_if(detail.rbegin(), detail.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), detail.end());
    std::string compact;
    compact.reserve(detail.size());
    bool previousWasSpace = false;
    for (const char ch : detail)
    {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            if (!previousWasSpace)
            {
                compact.push_back(' ');
                previousWasSpace = true;
            }
            continue;
        }
        compact.push_back(ch);
        previousWasSpace = false;
    }
    return compact.empty() ? "none" : compact;
}

bool isCriticalDeviceMissingEvidence(const std::string &detail)
{
    return detail.find("Critical device nodes missing:") != std::string::npos;
}

bool writeTextFileAtomically(const fs::path &path, const std::string &content, std::string *errorMessage);
bool loadJsonFile(const std::string &path, json *output, std::string *errorMessage);
std::string resolveDeviceNodeTarget(const std::string &devicePath);

const char *boolText(bool value)
{
    return value ? "1" : "0";
}

std::string sanitizeFileComponent(const std::string &text)
{
    std::string result;
    result.reserve(text.size());
    for (const char ch : text)
    {
        if ((ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            ch == '.' || ch == '_' || ch == '-')
        {
            result.push_back(ch);
        }
        else
        {
            result.push_back('_');
        }
    }
    return result.empty() ? "unknown" : result;
}

fs::path tactileReferenceDir(const fs::path &root)
{
    return root / "reference";
}

fs::path tactilePersistentDir(const fs::path &root)
{
    return root / "persistent";
}

fs::path episodeTimingPath(const fs::path &episodeDir)
{
    const std::string parent = sanitizeFileComponent(episodeDir.parent_path().filename().string());
    const std::string name = sanitizeFileComponent(episodeDir.filename().string());
    return fs::path("/dev/shm") / (std::string(kEpisodeTimingShmPrefix) + parent + "_" + name + ".json");
}

fs::path cameraRecorderFaultPath(const fs::path &episodeDir)
{
    const std::string parent = sanitizeFileComponent(episodeDir.parent_path().filename().string());
    const std::string name = sanitizeFileComponent(episodeDir.filename().string());
    return fs::path("/dev/shm") / (std::string(kCameraRecorderFaultShmPrefix) + parent + "_" + name + ".json");
}

std::string stripEpisodeTempSuffix(const std::string &name)
{
    constexpr const char *suffix = "-temp";
    constexpr size_t suffixLen = 5;
    if (name.size() >= suffixLen && name.compare(name.size() - suffixLen, suffixLen, suffix) == 0)
    {
        return name.substr(0, name.size() - suffixLen);
    }
    return name;
}

fs::path tactileHistoryPath(const fs::path &root)
{
    return root / "history.json";
}

fs::path tactilePersistentResetPath(const fs::path &root)
{
    return root / "persistent_reset.json";
}

bool isFalseLikeValue(const std::string &value)
{
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered == "0" || lowered == "false" || lowered == "no" ||
           lowered == "off" || lowered == "disable" || lowered == "disabled";
}

bool isStereoDevicePath(const char *path)
{
    return path != nullptr &&
           (std::strstr(path, "/stereo_") != nullptr || std::strstr(path, "_fays_imu") != nullptr);
}

std::vector<const char *> activeCriticalDevicePaths(bool chestCameraEnabled, bool stereoEnabled)
{
    std::vector<const char *> paths;
    paths.reserve(kAllCriticalDevicePaths.size());
    for (const char *path : kAllCriticalDevicePaths)
    {
        if (!chestCameraEnabled && std::strcmp(path, "/dev/cam_chest") == 0)
        {
            continue;
        }
        if (!stereoEnabled && isStereoDevicePath(path))
        {
            continue;
        }
        paths.push_back(path);
    }
    return paths;
}

std::vector<std::string> sessionCameraStreams(bool chestCameraEnabled)
{
    std::vector<std::string> streams = {
        "left_cam_main",
        "right_cam_main",
        "left_tcam_l",
        "left_tcam_r",
        "right_tcam_l",
        "right_tcam_r",
    };
    if (chestCameraEnabled)
    {
        streams.insert(streams.begin() + 2, "chest_cam_main");
    }
    return streams;
}

std::string joinCsv(const std::vector<std::string> &items)
{
    std::ostringstream stream;
    for (size_t index = 0; index < items.size(); ++index)
    {
        if (index > 0)
        {
            stream << ",";
        }
        stream << items[index];
    }
    return stream.str();
}

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

    logPerf((::DA::utils::LogString() << "[PERF] flush begin: phase=" << phaseLabel
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

        logPerf((::DA::utils::LogString() << "[PERF] flush done: phase=" << phaseLabel
                  << " path=" << path
                  << " elapsed_ms=" << (steadyNowMs() - artifactFlushStartMs)).str());
    }

    if (phase == "final" || phase == "ego_cleanup")
    {
        flushDirectoryTreeToDisk(egoDir, phaseLabel);
    }
    flushEpisodeDirectoriesToDisk(episodeDir, phaseLabel);

    logPerf((::DA::utils::LogString() << "[PERF] flush end: phase=" << phaseLabel
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

bool commandExists(const std::string &command)
{
    if (command.empty())
    {
        return false;
    }
    if (command.find('/') != std::string::npos)
    {
        return access(command.c_str(), X_OK) == 0;
    }

    const char *pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr)
    {
        return false;
    }

    std::stringstream stream(pathEnv);
    std::string directory;
    while (std::getline(stream, directory, ':'))
    {
        if (directory.empty())
        {
            directory = ".";
        }
        const fs::path candidate = fs::path(directory) / command;
        if (access(candidate.c_str(), X_OK) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string> resolvePythonCommand()
{
    if (commandExists("./.venv/bin/python3"))
    {
        return {"./.venv/bin/python3"};
    }
    if (commandExists("uv"))
    {
        return {"uv", "run", "python3"};
    }
    return {"python3"};
}

std::string trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string asciiLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

std::string resolveEgoAdbCommand(const std::string &egoRecordingScript)
{
    fs::path bundled;
    if (!egoRecordingScript.empty())
    {
        bundled = fs::path(egoRecordingScript).parent_path().parent_path() / "adb" / "adb";
    }
    if (!bundled.empty() && access(bundled.c_str(), X_OK) == 0)
    {
        return bundled.string();
    }
    return "adb";
}

std::string readTrimmedSysfsText(const fs::path &path)
{
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value))
    {
        return {};
    }
    return asciiLower(trim(value));
}

bool hasAdbUsbInterface()
{
    std::error_code error;
    const fs::path usbRoot("/sys/bus/usb/devices");
    if (!fs::exists(usbRoot, error))
    {
        return true;
    }

    for (const auto &entry : fs::directory_iterator(usbRoot, fs::directory_options::skip_permission_denied, error))
    {
        const std::string interfaceClass = readTrimmedSysfsText(entry.path() / "bInterfaceClass");
        const std::string interfaceSubClass = readTrimmedSysfsText(entry.path() / "bInterfaceSubClass");
        const std::string interfaceProtocol = readTrimmedSysfsText(entry.path() / "bInterfaceProtocol");
        if (interfaceClass == "ff" && interfaceSubClass == "42" && interfaceProtocol == "01")
        {
            return true;
        }
    }
    if (error)
    {
        return true;
    }
    return false;
}

std::optional<std::string> readTextFileTrimmed(const fs::path &path, std::string *detail)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        if (detail != nullptr)
        {
            *detail = "cannot open " + path.string();
        }
        return std::nullopt;
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    std::string value = trim(stream.str());
    if (value.empty())
    {
        if (detail != nullptr)
        {
            *detail = "empty " + path.string();
        }
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> readTactileUsbSerialFromSysfs(const std::string &devicePath,
                                                        const std::string &resolvedTarget,
                                                        std::string *detail)
{
    if (!fs::exists(devicePath))
    {
        if (detail != nullptr)
        {
            *detail = "device node missing";
        }
        return std::nullopt;
    }

    fs::path videoNode = resolvedTarget.empty() ? fs::path(resolveDeviceNodeTarget(devicePath))
                                                : fs::path(resolvedTarget);
    const std::string videoName = videoNode.filename().string();
    if (videoName.rfind("video", 0) != 0)
    {
        if (detail != nullptr)
        {
            *detail = "resolved target is not a video node: " + videoNode.string();
        }
        return std::nullopt;
    }

    std::error_code error;
    fs::path deviceSysfs = fs::canonical(fs::path("/sys/class/video4linux") / videoName / "device", error);
    if (error)
    {
        if (detail != nullptr)
        {
            *detail = "cannot resolve video sysfs device for " + videoName + ": " + error.message();
        }
        return std::nullopt;
    }

    for (fs::path current = deviceSysfs; !current.empty(); current = current.parent_path())
    {
        if (current == current.parent_path())
        {
            break;
        }

        const fs::path serialPath = current / "serial";
        if (!fs::exists(serialPath, error) || error)
        {
            error.clear();
            continue;
        }

        std::string readDetail;
        const auto serial = readTextFileTrimmed(serialPath, &readDetail);
        if (serial.has_value() && serial->rfind("xhci-", 0) != 0)
        {
            return serial;
        }
    }

    if (detail != nullptr)
    {
        *detail = "no USB serial found under sysfs for " + videoName;
    }
    return std::nullopt;
}

bool isYuzhouMainCameraSn(const std::string &sn)
{
    if (sn.size() != kYuzhouSnLength || sn.rfind("FE", 0) != 0)
    {
        return false;
    }
    return std::all_of(sn.begin() + 2, sn.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool readYuzhouXuChunk(int fd, size_t chunkIndex, uint8_t *chunkData, std::string *detail)
{
    if (chunkIndex >= kYuzhouPayloadChunkCount)
    {
        if (detail != nullptr)
        {
            *detail = "chunk index out of range: " + std::to_string(chunkIndex);
        }
        return false;
    }

    std::array<uint8_t, kYuzhouXuPacketSize> readIndexCommand = {
        0x97, 0x97, 0x97, 0x97, 0x97, 0x97, 0x97, 0x01, static_cast<uint8_t>(chunkIndex)};
    struct uvc_xu_control_query setQuery = {};
    setQuery.unit = kYuzhouXuUnitId;
    setQuery.selector = kYuzhouXuSelector;
    setQuery.query = UVC_SET_CUR;
    setQuery.size = kYuzhouXuPacketSize;
    setQuery.data = readIndexCommand.data();
    if (ioctl(fd, UVCIOC_CTRL_QUERY, &setQuery) < 0)
    {
        if (detail != nullptr)
        {
            *detail = "UVC SET_CUR failed at chunk " + std::to_string(chunkIndex) + ": " +
                      std::strerror(errno);
        }
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::array<uint8_t, kYuzhouXuPacketSize> response{};
    struct uvc_xu_control_query getQuery = {};
    getQuery.unit = kYuzhouXuUnitId;
    getQuery.selector = kYuzhouXuSelector;
    getQuery.query = UVC_GET_CUR;
    getQuery.size = kYuzhouXuPacketSize;
    getQuery.data = response.data();
    if (ioctl(fd, UVCIOC_CTRL_QUERY, &getQuery) < 0)
    {
        if (detail != nullptr)
        {
            *detail = "UVC GET_CUR failed at chunk " + std::to_string(chunkIndex) + ": " +
                      std::strerror(errno);
        }
        return false;
    }

    std::memcpy(chunkData, response.data() + 1, kYuzhouPayloadChunkSize);
    return true;
}

bool validateYuzhouMainCameraCalibrationPayload(const std::array<uint8_t, kYuzhouPayloadSize> &payload,
                                                std::string *detail)
{
    main_camera::MainCameraCalibrationDataV1 data{};
    std::memcpy(&data, payload.data(), sizeof(data));
    if (std::memcmp(data.header.magic, "MCAL", 4) != 0)
    {
        if (detail != nullptr)
        {
            *detail = "invalid MCAL payload magic";
        }
        return false;
    }
    if (data.header.dataFormatVersion != main_camera::kCalibrationDataFormatVersion1)
    {
        if (detail != nullptr)
        {
            *detail = "unsupported MCAL data_format_version=" +
                      std::to_string(data.header.dataFormatVersion);
        }
        return false;
    }
    if (data.header.headerSize != sizeof(main_camera::MainCameraCalibrationHeader))
    {
        if (detail != nullptr)
        {
            *detail = "invalid MCAL header_size=" + std::to_string(data.header.headerSize);
        }
        return false;
    }
    const uint16_t minimumPayloadSize =
        static_cast<uint16_t>(sizeof(main_camera::MainCameraCalibrationHeader) +
                              sizeof(main_camera::MainCameraIntrinsicsBlock));
    if (data.header.payloadSize < minimumPayloadSize ||
        data.header.payloadSize > main_camera::kCalibrationPayloadSize)
    {
        if (detail != nullptr)
        {
            *detail = "invalid MCAL payload_size=" + std::to_string(data.header.payloadSize);
        }
        return false;
    }

    constexpr uint32_t requiredFields =
        main_camera::kCalibrationValidCameraModel |
        main_camera::kCalibrationValidDistortion |
        main_camera::kCalibrationValidIntrinsics |
        main_camera::kCalibrationValidResolution;
    if ((data.header.validFields & requiredFields) != requiredFields)
    {
        if (detail != nullptr)
        {
            *detail = "MCAL valid_fields missing required bits";
        }
        return false;
    }

    if (data.mainCamera.resolution[0] <= 0.0f || data.mainCamera.resolution[1] <= 0.0f ||
        data.mainCamera.intrinsics[0] <= 0.0f || data.mainCamera.intrinsics[1] <= 0.0f)
    {
        if (detail != nullptr)
        {
            *detail = "MCAL intrinsics or resolution is empty";
        }
        return false;
    }
    return true;
}

std::optional<YuzhouMainCameraIdentity> readYuzhouMainCameraIdentity(const std::string &cameraName,
                                                                    const std::string &devicePath,
                                                                    const std::string &resolvedTarget,
                                                                    std::string *detail)
{
    if (!fs::exists(devicePath))
    {
        if (detail != nullptr)
        {
            *detail = "device node missing";
        }
        return std::nullopt;
    }

    const int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        if (detail != nullptr)
        {
            *detail = std::string("open failed: ") + std::strerror(errno);
        }
        return std::nullopt;
    }

    std::array<uint8_t, kYuzhouPayloadSize> payload{};
    for (size_t chunkIndex = 0; chunkIndex < 10; ++chunkIndex)
    {
        if (!readYuzhouXuChunk(fd,
                               chunkIndex,
                               payload.data() + chunkIndex * kYuzhouPayloadChunkSize,
                               detail))
        {
            close(fd);
            return std::nullopt;
        }
    }

    close(fd);

    std::string calibrationDetail;
    if (!validateYuzhouMainCameraCalibrationPayload(payload, &calibrationDetail))
    {
        if (detail != nullptr)
        {
            *detail = calibrationDetail;
        }
        return std::nullopt;
    }

    const std::string sn(reinterpret_cast<const char *>(payload.data() + kYuzhouSnOffset), kYuzhouSnLength);
    if (!isYuzhouMainCameraSn(sn))
    {
        if (detail != nullptr)
        {
            *detail = "invalid or empty Yuzhou SN field";
        }
        return std::nullopt;
    }
    if (detail != nullptr)
    {
        *detail = "yuzhou_xu_fast";
    }

    YuzhouMainCameraIdentity result;
    result.cameraName = cameraName;
    result.devicePath = devicePath;
    result.resolvedTarget = resolvedTarget;
    result.serialNumber = sn;
    result.calibrationPayloadCached = true;
    result.calibrationPayload = payload;
    return result;
}

std::string probeMainCameraSerialForDeviceNode(const std::string &devicePath)
{
    std::string detail;
    const auto identity = readYuzhouMainCameraIdentity("", devicePath, "", &detail);
    if (identity.has_value())
    {
        return identity->serialNumber;
    }
    DM_LOG_WARN("{}", (::DA::utils::LogString()
                       << "failed to read Yuzhou XU SN from " << devicePath
                       << ": " << detail << std::endl).str());
    return "";
}

std::string resolveDeviceNodeTarget(const std::string &devicePath)
{
    std::error_code error;
    fs::path target;
    if (fs::is_symlink(devicePath, error))
    {
        target = fs::read_symlink(devicePath, error);
        if (!error)
        {
            if (target.is_relative())
            {
                target = fs::path(devicePath).parent_path() / target;
            }
            return target.lexically_normal().string();
        }
    }
    target = fs::canonical(devicePath, error);
    if (!error)
    {
        return target.string();
    }
    return devicePath;
}

}  // namespace ugripper::runtime::record_runtime_detail
