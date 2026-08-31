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
