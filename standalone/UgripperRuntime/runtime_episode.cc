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

json makeRuntimeTactileCalibrationEntry(const std::string &serial)
{
    return json::object({
        {"names", json::array({"height", "width", "channels"})},
        {"serial", serial},
        {"shape", json::array({480, 640, 3})},
    });
}

json makeMainCameraCalibrationEntryFromPayload(const std::array<uint8_t, 1024> &payload)
{
    main_camera::MainCameraCalibrationDataV1 data{};
    std::memcpy(&data, payload.data(), sizeof(data));
    const int width = static_cast<int>(std::lround(data.mainCamera.resolution[0]));
    const int height = static_cast<int>(std::lround(data.mainCamera.resolution[1]));
    const int normalizedWidth = width > 0 ? width : 1920;
    const int normalizedHeight = height > 0 ? height : 1080;
    return json::object({
        {"camera_model", "pinhole"},
        {"distortion_coeffs",
         json::array({
             data.mainCamera.distortionCoefficients[0],
             data.mainCamera.distortionCoefficients[1],
             data.mainCamera.distortionCoefficients[2],
             data.mainCamera.distortionCoefficients[3],
         })},
        {"distortion_model", "equidistant"},
        {"fps", 60.0},
        {"intrinsics",
         json::object({{std::to_string(normalizedWidth) + "x" +
                             std::to_string(normalizedHeight),
                         json::object({
                             {"fx", data.mainCamera.intrinsics[0]},
                             {"fy", data.mainCamera.intrinsics[1]},
                             {"ppx", data.mainCamera.intrinsics[2]},
                             {"ppy", data.mainCamera.intrinsics[3]},
                         })}})},
        {"names", json::array({"height", "width", "channels"})},
        {"shape", json::array({normalizedHeight, normalizedWidth, 3})},
    });
}

json makeDefaultMainCameraCalibrationEntry()
{
    return json::object({
        {"camera_model", "pinhole"},
        {"distortion_coeffs", json::array({0.0, 0.0, 0.0, 0.0})},
        {"distortion_model", "equidistant"},
        {"fps", 60.0},
        {"intrinsics",
         json::object({
             {"1920x1080",
              json::object({
                  {"fx", 1920.0},
                  {"fy", 1080.0},
                  {"ppx", 960.0},
                  {"ppy", 540.0},
              })},
         })},
        {"names", json::array({"height", "width", "channels"})},
        {"shape", json::array({1080, 1920, 3})},
    });
}

bool loadFaysCalibrationFromStatus(const std::string &stereoStatusFile,
                                   const std::string &cameraName,
                                   json *calibration,
                                   std::string *statusMessage)
{
    if (calibration == nullptr)
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = "calibration output is null";
        }
        return false;
    }

    json status;
    std::string loadError;
    if (!loadJsonFile(stereoStatusFile, &status, &loadError))
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = loadError;
        }
        return false;
    }

    const json *camera = findJsonPathConst(&status, std::string("cameras.") + cameraName);
    if (camera == nullptr || !camera->is_object())
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = "stereo status missing camera entry: " + cameraName;
        }
        return false;
    }
    if (!camera->contains("calibration") || !(*camera)["calibration"].is_object())
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = "stereo status missing calibration entry: " + cameraName;
        }
        return false;
    }

    const json &calibrationState = (*camera)["calibration"];
    if (!calibrationState.value("valid", false))
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = "Fays calibration not ready for " + cameraName +
                             ": " + calibrationState.value("status", std::string("unknown"));
        }
        return false;
    }

    const std::string path = calibrationState.value("path", std::string());
    if (path.empty())
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = "Fays calibration path is empty for " + cameraName;
        }
        return false;
    }

    json faysCalibration;
    if (!loadJsonFile(path, &faysCalibration, &loadError))
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = loadError;
        }
        return false;
    }
    if (!faysCalibration.is_object() || !faysCalibration.value("valid", false))
    {
        if (statusMessage != nullptr)
        {
            *statusMessage = "invalid Fays calibration payload: " + path;
        }
        return false;
    }

    faysCalibration.erase("valid");
    if (!faysCalibration.contains("dtype"))
    {
        faysCalibration["dtype"] = "video";
    }
    if (!faysCalibration.contains("fps"))
    {
        faysCalibration["fps"] = 25;
    }
    *calibration = std::move(faysCalibration);
    if (statusMessage != nullptr)
    {
        *statusMessage = "loaded " + path;
    }
    return true;
}

json makeFaysCameraCalibrationEntry(const json &camera)
{
    const auto normalizedDistortionCoeffs = [](const json &source, const std::string &model) {
        const size_t coeffCount =
            (model == "ADM_BROWN_CONRADY") ? 5u :
            (model == "ADM_CVBASIC") ? 8u :
            4u;
        json coeffs = json::array();
        if (source.is_array())
        {
            for (size_t index = 0; index < coeffCount && index < source.size(); ++index)
            {
                coeffs.push_back(source[index]);
            }
        }
        while (coeffs.size() < coeffCount)
        {
            coeffs.push_back(0.0);
        }
        return coeffs;
    };

    const std::string distortionModel = camera.value("distortion_model", std::string("ADM_KB4"));
    json entry = json::object();
    entry["intrinsics"] = camera.contains("intrinsics") && camera["intrinsics"].is_object() && !camera["intrinsics"].empty()
                              ? camera["intrinsics"]
                              : json::object({
                                    {"640x400",
                                     json::object({
                                         {"fx", 640.0},
                                         {"fy", 400.0},
                                         {"ppx", 320.0},
                                         {"ppy", 200.0},
                                     })},
                                });
    entry["distortion_coeffs"] = normalizedDistortionCoeffs(
        camera.contains("distortion_coeffs") ? camera["distortion_coeffs"] : json::array(),
        distortionModel);
    return entry;
}

json makeFaysStereoCalibrationEntry(const json &faysCalibration)
{
    const auto normalizeDistortionModel = [](const json &model) {
        const std::string value = model.is_string() ? model.get<std::string>() : std::string("ADM_KB4");
        if (value == "ADM_KB4")
        {
            return json("equidistant");
        }
        if (value == "ADM_RADTAN")
        {
            return json("radtan");
        }
        if (value == "ADM_BROWN_CONRADY")
        {
            return json("brown_conrady");
        }
        if (value == "ADM_CVBASIC")
        {
            return json("opencv_basic");
        }
        return model.is_string() ? model : json("equidistant");
    };

    json stereo = json::object();
    stereo["cam0"] = makeFaysCameraCalibrationEntry(
        faysCalibration.contains("cam0") && faysCalibration["cam0"].is_object()
            ? faysCalibration["cam0"]
            : json::object());
    stereo["cam1"] = makeFaysCameraCalibrationEntry(
        faysCalibration.contains("cam1") && faysCalibration["cam1"].is_object()
            ? faysCalibration["cam1"]
            : json::object());
    stereo["camera_model"] = faysCalibration.contains("camera_model") ? faysCalibration["camera_model"] : json("pinhole");
    stereo["distortion_model"] = normalizeDistortionModel(
        faysCalibration.contains("distortion_model") ? faysCalibration["distortion_model"] : json("ADM_KB4"));
    stereo["extrinsics"] = faysCalibration.contains("extrinsics") && faysCalibration["extrinsics"].is_object()
                               ? faysCalibration["extrinsics"]
                               : json::object();
    stereo["fps"] = 25.0;
    stereo["names"] = json::array({"height", "width", "channels"});
    stereo["shape"] = json::array({800, 640, 3});
    return stereo;
}

json makeDefaultStereoCalibrationEntry()
{
    const auto makeCamera = []() {
        return json::object({
            {"distortion_coeffs", json::array({0.0, 0.0, 0.0, 0.0})},
            {"intrinsics",
             json::object({
                 {"640x400",
                  json::object({
                      {"fx", 640.0},
                      {"fy", 400.0},
                      {"ppx", 320.0},
                      {"ppy", 200.0},
                  })},
             })},
        });
    };
    const auto identity = []() {
        return json::array({
            json::array({1.0, 0.0, 0.0, 0.0}),
            json::array({0.0, 1.0, 0.0, 0.0}),
            json::array({0.0, 0.0, 1.0, 0.0}),
            json::array({0.0, 0.0, 0.0, 1.0}),
        });
    };
    return json::object({
        {"cam0", makeCamera()},
        {"cam1", makeCamera()},
        {"camera_model", "pinhole"},
        {"distortion_model", "equidistant"},
        {"extrinsics",
         json::object({
             {"T_ic_cam0_to_imu0", identity()},
             {"T_ic_cam1_to_imu0", identity()},
             {"timeshift_cam0_to_imu0", 0.0},
             {"timeshift_cam1_to_imu0", 0.0},
         })},
        {"fps", 25.0},
        {"names", json::array({"height", "width", "channels"})},
        {"shape", json::array({800, 640, 3})},
    });
}

json makeFaysImuCalibrationEntry(const json &faysCalibration)
{
    json imu = json::object();
    if (faysCalibration.contains("imu") && faysCalibration["imu"].is_object())
    {
        const json &source = faysCalibration["imu"];
        if (source.contains("update_rate"))
        {
            imu["update_rate_hz"] = source["update_rate"];
        }
        if (source.contains("accelerometer_noise_density") || source.contains("accelerometer_random_walk"))
        {
            imu["accelerometer"] = json::object();
            if (source.contains("accelerometer_noise_density"))
            {
                imu["accelerometer"]["noise_density_discrete"] = source["accelerometer_noise_density"];
            }
            if (source.contains("accelerometer_random_walk"))
            {
                imu["accelerometer"]["random_walk"] = source["accelerometer_random_walk"];
            }
        }
        if (source.contains("gyroscope_noise_density") || source.contains("gyroscope_random_walk"))
        {
            imu["gyroscope"] = json::object();
            if (source.contains("gyroscope_noise_density"))
            {
                imu["gyroscope"]["noise_density_discrete"] = source["gyroscope_noise_density"];
            }
            if (source.contains("gyroscope_random_walk"))
            {
                imu["gyroscope"]["random_walk"] = source["gyroscope_random_walk"];
            }
        }
    }
    return imu;
}

json makeDefaultFaysImuCalibrationEntry()
{
    return json::object({
        {"accelerometer",
         json::object({
             {"noise_density_discrete", 0.0},
             {"random_walk", 0.0},
         })},
        {"gyroscope",
         json::object({
             {"noise_density_discrete", 0.0},
             {"random_walk", 0.0},
         })},
        {"update_rate_hz", 0},
    });
}

json makeLockedCalibrationInfo(const std::string &calibrationStatus)
{
    return json::object({
        {"calibration_status", calibrationStatus},
        {"format_version", "3.0"},
    });
}

}  // namespace ugripper::runtime::record_runtime_detail

ugripper::runtime::EpisodeManager::EpisodeManager(std::string diskRoot,
                                              std::string deviceSn,
                                              std::string language,
                                              std::string cameraCodec,
                                              bool chestCameraEnabled,
                                              bool stereoEnabled,
                                              std::string tactileStateDir,
                                              std::string persistCalibrationFile,
                                              std::string exampleCalibrationFile,
                                              std::string fallbackCalibrationFile,
                                              std::string stereoStatusFile,
                                              std::string hardwareVersion,
                                              std::string packageVersion,
                                              std::string updaterVersion)
    : diskRoot_(std::move(diskRoot)),
      deviceSn_(std::move(deviceSn)),
      deviceSnLower_(RecordRuntime::toLower(deviceSn_)),
      language_(std::move(language)),
      cameraCodec_(std::move(cameraCodec)),
      chestCameraEnabled_(chestCameraEnabled),
      stereoEnabled_(stereoEnabled),
      tactileStateDir_(std::move(tactileStateDir)),
      persistCalibrationFile_(std::move(persistCalibrationFile)),
      exampleCalibrationFile_(std::move(exampleCalibrationFile)),
      fallbackCalibrationFile_(std::move(fallbackCalibrationFile)),
      stereoStatusFile_(std::move(stereoStatusFile)),
      hardwareVersion_(std::move(hardwareVersion)),
      packageVersion_(std::move(packageVersion)),
      updaterVersion_(std::move(updaterVersion))
{
    dataRoot_ = diskRoot_ + "/" + deviceSnLower_;
    episodeRoot_ = dataRoot_ + "/data";
}

bool ugripper::runtime::EpisodeManager::initialize()
{
    std::error_code error;
    fs::create_directories(episodeRoot_, error);
    if (error)
    {
        DM_LOG_ERROR_CODE("UGR-10-02", "{}", (::DA::utils::LogString() << "create_directories failed for " << episodeRoot_
                  << ": " << error.message() << std::endl).str());
        return false;
    }
    fs::create_directories(tactileReferenceDir(tactileStateDir_), error);
    if (error)
    {
        DM_LOG_ERROR_CODE("UGR-10-02", "{}", (::DA::utils::LogString() << "create_directories failed for "
                              << tactileReferenceDir(tactileStateDir_) << ": "
                              << error.message() << std::endl).str());
        return false;
    }
    fs::create_directories(tactilePersistentDir(tactileStateDir_), error);
    if (error)
    {
        DM_LOG_ERROR_CODE("UGR-10-02", "{}", (::DA::utils::LogString() << "create_directories failed for "
                              << tactilePersistentDir(tactileStateDir_) << ": "
                              << error.message() << std::endl).str());
        return false;
    }
    return true;
}

void ugripper::runtime::EpisodeManager::setGripperRuntimeStates(
    const std::array<GripperRuntimeState, 2> &states)
{
    gripperRuntimeStates_ = states;
}

void ugripper::runtime::EpisodeManager::setMainCameraRuntimeStates(
    const std::array<MainCameraRuntimeState, 3> &states)
{
    mainCameraRuntimeStates_ = states;
}

void ugripper::runtime::EpisodeManager::setTactileCameraRuntimeStates(
    const std::array<TactileCameraRuntimeState, 4> &states)
{
    tactileCameraRuntimeStates_ = states;
}

const ugripper::runtime::EpisodeManager::TactileCameraRuntimeState *
ugripper::runtime::EpisodeManager::tactileCameraStateForName(const std::string &cameraName) const
{
    const auto it = std::find_if(
        tactileCameraRuntimeStates_.begin(),
        tactileCameraRuntimeStates_.end(),
        [&cameraName](const TactileCameraRuntimeState &state) {
            return state.cameraName == cameraName;
        });
    return it != tactileCameraRuntimeStates_.end() ? &(*it) : nullptr;
}

std::string ugripper::runtime::EpisodeManager::cachedTactileSerialForName(const std::string &cameraName) const
{
    const auto *state = tactileCameraStateForName(cameraName);
    return state != nullptr ? state->serialNumber : std::string();
}

std::string ugripper::runtime::EpisodeManager::createNextEpisodeDir()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&nowTime, &localTime);

    char dateBuffer[16] = {0};
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y%m%d", &localTime);
    const std::string prefix = std::string("episode_") + dateBuffer + "_";

    int maxId = 0;
    std::error_code error;
    for (const auto &entry : fs::directory_iterator(episodeRoot_, error))
    {
        if (error || !entry.is_directory())
        {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0)
        {
            continue;
        }
        const std::string normalizedName = stripEpisodeTempSuffix(name);
        if (normalizedName.rfind(prefix, 0) != 0)
        {
            continue;
        }
        const std::string suffix = normalizedName.substr(prefix.size());
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }))
        {
            maxId = std::max(maxId, std::stoi(suffix));
        }
    }

    const int nextId = maxId + 1;
    char idBuffer[16] = {0};
    std::snprintf(idBuffer, sizeof(idBuffer), "%04d", nextId);
    const fs::path episodePath = fs::path(episodeRoot_) / (prefix + idBuffer + "-temp");
    fs::create_directories(episodePath, error);
    if (error)
    {
        return {};
    }
    flushEpisodeDirectoriesToDisk(episodePath, "create_episode_dir");
    return episodePath.string();
}

void ugripper::runtime::EpisodeManager::markTactileReferencePendingForSide(
    const std::string &side,
    const std::string &reason)
{
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
    const uint64_t nowMs = RecordRuntime::currentEpochMs();
    for (const auto &target : kTactileCalibrationTargets)
    {
        if (side != target.side)
        {
            continue;
        }

        const auto *state = tactileCameraStateForName(target.cameraName);
        const std::string serial = state != nullptr ? state->serialNumber : std::string();
        if (serial.empty())
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString() << "tactile reference pending skipped: camera=" << target.cameraName
                                 << " reason="
                                 << (state == nullptr
                                         ? "missing tactile serial cache state"
                                         : (state->lastError.empty() ? "missing tactile serial cache" : state->lastError))
                                 << std::endl).str());
            continue;
        }

        json &cameraHistory = tactileHistory["cameras"][serial];
        if (!cameraHistory.is_object())
        {
            cameraHistory = json::object();
        }
        cameraHistory["camera_name"] = target.cameraName;
        cameraHistory["reference_pending"] = true;
        cameraHistory["reference_pending_reason"] = reason;
        cameraHistory["reference_pending_at_ms"] = nowMs;
        cameraHistory["recent"] = json::array();
        historyDirty = true;
        DM_LOG_INFO("{}", (::DA::utils::LogString()
            << "tactile reference pending: camera=" << target.cameraName
            << " side=" << side
            << " serial=" << serial
            << " reason=" << reason
            << std::endl).str());
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
}

bool ugripper::runtime::EpisodeManager::prepareEpisode(const std::string &episodeDir,
                                                   bool resetRecording,
                                                   const std::string &resetSourceDir,
                                                   std::string *errorMessage) const
{
    if (!writeFilteredCalibration(episodeDir, errorMessage))
    {
        return false;
    }
    if (!prepareEpisodeOutputs(episodeDir, errorMessage))
    {
        return false;
    }
    return true;
}

namespace
{
std::string toUpperAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string addVersionPrefix(const std::string &version)
{
    if (version.empty() || version == "unknown" || version.front() == 'v' || version.front() == 'V')
    {
        return version;
    }
    return "v" + version;
}

double roundToOneDecimal(double value)
{
    if (!std::isfinite(value))
    {
        return 0.0;
    }
    return std::round(value * 10.0) / 10.0;
}

std::string generateUuid()
{
    std::ifstream input("/proc/sys/kernel/random/uuid");
    std::string uuid;
    if (input.is_open() && std::getline(input, uuid))
    {
        uuid = trim(uuid);
        if (!uuid.empty())
        {
            return uuid;
        }
    }

    uuid_t fallbackUuid;
    char uuidString[37] = {0};
    uuid_generate(fallbackUuid);
    uuid_unparse(fallbackUuid, uuidString);
    return std::string(uuidString);
}

double nominalFpsForCamera(const std::string &cameraName)
{
    if (cameraName.find("tcam") != std::string::npos)
    {
        return 120.0;
    }
    if (cameraName.find("stereo") != std::string::npos)
    {
        return 25.0;
    }
    return 60.0;
}

std::vector<EpisodeVideoArtifact> metadataVideoArtifacts(bool chestCameraEnabled, bool stereoEnabled)
{
    std::vector<EpisodeVideoArtifact> artifacts = {
        {"left_cam_main", "cam_left.mkv"},
        {"left_tcam_l", "tcam_left_l.mkv"},
        {"left_tcam_r", "tcam_left_r.mkv"},
        {"left_stereo", "stereo_left.mkv"},
        {"right_cam_main", "cam_right.mkv"},
        {"right_tcam_l", "tcam_right_l.mkv"},
        {"right_tcam_r", "tcam_right_r.mkv"},
        {"right_stereo", "stereo_right.mkv"},
    };
    if (chestCameraEnabled)
    {
        artifacts.push_back({"chest_cam_main", "cam_chest.mkv"});
    }
    if (!stereoEnabled)
    {
        artifacts.erase(
            std::remove_if(
                artifacts.begin(), artifacts.end(), [](const EpisodeVideoArtifact &artifact) {
                    return isStereoCameraName(artifact.cameraName);
                }),
            artifacts.end());
    }
    return artifacts;
}

std::string jsonStringPath(const json &root, const std::string &path)
{
    const json *value = findJsonPathConst(&root, path);
    if (value != nullptr && value->is_string())
    {
        return value->get<std::string>();
    }
    return "";
}
}

bool ugripper::runtime::EpisodeManager::writeFinalMetadata(const std::string &episodeDir,
                                                       bool qualityOk,
                                                       const std::string &qualityErrorMessage,
                                                       const std::string &qualityErrorType,
                                                       std::string *errorMessage) const
{
    const fs::path episodePath(episodeDir);
    json existingMetadata = json::object();
    std::string ignoredError;
    loadJsonFile((episodePath / "metadata.json").string(), &existingMetadata, &ignoredError);
    if (!existingMetadata.is_object())
    {
        existingMetadata = json::object();
    }

    ordered_json hardwareList = ordered_json::object();
    hardwareList["gripper_right_sn"] = gripperRuntimeStates_[gripperStateIndexForSide("right")].serialNumber;
    hardwareList["gripper_left_sn"] = gripperRuntimeStates_[gripperStateIndexForSide("left")].serialNumber;

    const auto cachedMainCameraSn = [this](const std::string &cameraName) {
        const auto it = std::find_if(
            mainCameraRuntimeStates_.begin(),
            mainCameraRuntimeStates_.end(),
            [&cameraName](const MainCameraRuntimeState &state) {
                return state.cameraName == cameraName;
            });
        return it != mainCameraRuntimeStates_.end() ? it->serialNumber : std::string();
    };
    hardwareList["cam_right_sn"] = cachedMainCameraSn("right_cam_main");
    hardwareList["cam_left_sn"] = cachedMainCameraSn("left_cam_main");
    hardwareList["cam_chest_sn"] = chestCameraEnabled_ ? cachedMainCameraSn("chest_cam_main") : "";
    hardwareList["tactile_right_l_sn"] = cachedTactileSerialForName("right_tcam_l");
    hardwareList["tactile_right_r_sn"] = cachedTactileSerialForName("right_tcam_r");
    hardwareList["tactile_left_l_sn"] = cachedTactileSerialForName("left_tcam_l");
    hardwareList["tactile_left_r_sn"] = cachedTactileSerialForName("left_tcam_r");

    json stereoStatus = json::object();
    if (stereoEnabled_)
    {
        loadJsonFile(stereoStatusFile_, &stereoStatus, &ignoredError);
    }
    hardwareList["stereo_right_sn"] =
        stereoEnabled_ ? jsonStringPath(stereoStatus, "cameras.right_stereo.serial_number") : "";
    hardwareList["stereo_left_sn"] =
        stereoEnabled_ ? jsonStringPath(stereoStatus, "cameras.left_stereo.serial_number") : "";

    json infoRoot = json::object();
    loadJsonFile(episodeTimingPath(episodePath).string(), &infoRoot, &ignoredError);
    std::map<std::string, int64_t> offsetByCamera;
    for (const auto &artifact : metadataVideoArtifacts(chestCameraEnabled_, stereoEnabled_))
    {
        const std::string key = std::string(artifact.cameraName) + "_record_time_offset_us";
        if (infoRoot.contains(key) && infoRoot[key].is_number_integer())
        {
            const int64_t offsetUs = infoRoot[key].get<int64_t>();
            if (offsetUs > 0)
            {
                offsetByCamera[artifact.cameraName] = offsetUs;
            }
        }
    }

    if (qualityOk)
    {
        const auto metadataArtifacts = metadataVideoArtifacts(chestCameraEnabled_, stereoEnabled_);
        if (!infoRoot.is_object())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "internal shm timing file top-level value must be an object";
            }
            return false;
        }
        if (offsetByCamera.size() != metadataArtifacts.size())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "internal shm timing file missing video offset cache";
            }
            return false;
        }
    }

    ordered_json videoDetails = ordered_json::array();
    double collectionDurationS = 0.0;
    for (const auto &artifact : metadataVideoArtifacts(chestCameraEnabled_, stereoEnabled_))
    {
        const fs::path videoPath = episodePath / artifact.fileName;
        if (!RecordRuntime::fileExistsAndNotEmpty(videoPath.string()))
        {
            continue;
        }

        VideoProbeResult probe;
        if (!loadCachedVideoProbe(infoRoot, artifact, &probe))
        {
            probe.cameraName = artifact.cameraName;
            probe.fileName = artifact.fileName;
            std::string probeError;
            if (!probeVideoFile(videoPath.string(), &probe, &probeError))
            {
                continue;
            }
        }

        double detailDurationSec = probe.durationSec;
        std::string durationSource;
        std::string durationError;
        if (!loadEffectiveVideoDurationSec(
                episodeDir,
                artifact,
                probe,
                &infoRoot,
                probe.durationSec,
                &detailDurationSec,
                &durationSource,
                &durationError))
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                               << "failed to load effective video duration, falling back to mkv probe: "
                               << durationError << std::endl).str());
            detailDurationSec = probe.durationSec;
        }

        const auto offsetIt = offsetByCamera.find(artifact.cameraName);
        const int64_t startOffsetUs =
            offsetIt != offsetByCamera.end() ? offsetIt->second : 0;

        ordered_json detail = ordered_json::object();
        detail["name"] = artifact.fileName;
        detail["fps"] = roundToOneDecimal(nominalFpsForCamera(artifact.cameraName));
        detail["duration_s"] = roundToOneDecimal(detailDurationSec);
        detail["start_offset_us"] = startOffsetUs;
        videoDetails.push_back(std::move(detail));
        collectionDurationS = std::max(collectionDurationS, detailDurationSec);
    }

    ordered_json requiredFiles = ordered_json::array({
        "metadata.json",
        "calibration.json",
        "cam_left.mkv",
        "cam_right.mkv",
        "tcam_left_l.mkv",
        "tcam_left_r.mkv",
        "tcam_right_l.mkv",
        "tcam_right_r.mkv",
        "sensor_left.mcap",
        "sensor_right.mcap",
    });
    if (stereoEnabled_)
    {
        requiredFiles.push_back("stereo_left.mkv");
        requiredFiles.push_back("stereo_right.mkv");
        requiredFiles.push_back("fays_data_left.mcap");
        requiredFiles.push_back("fays_data_right.mcap");
    }
    if (chestCameraEnabled_)
    {
        requiredFiles.push_back("cam_chest.mkv");
    }

    const bool hasAudio =
        RecordRuntime::fileExistsAndNotEmpty((episodePath / "audio_pre.wav").string()) ||
        RecordRuntime::fileExistsAndNotEmpty((episodePath / "audio_post.wav").string());

    ordered_json metadata = ordered_json::object();
    metadata["device_sn"] = toUpperAscii(deviceSn_);
    metadata["device_type"] = "ugripper";
    metadata["device_mode"] = "dual";
    metadata["camera_codec"] = cameraCodec_;
    metadata["hardware_version"] = hardwareVersion_;
    metadata["software_version"] = addVersionPrefix(packageVersion_);
    metadata["das_usb_updater_version"] = updaterVersion_;
    metadata["data_version"] = "3.2";
    metadata["hardware_list"] = std::move(hardwareList);
    metadata["episode_name"] = stripEpisodeTempSuffix(episodePath.filename().string());
    metadata["data_uuid"] = existingMetadata.value("data_uuid", generateUuid());
    metadata["audio_uuid"] = hasAudio ? existingMetadata.value("audio_uuid", generateUuid()) : "";
    metadata["quality_check_status"] = qualityOk ? "success" : "fail";
    metadata["quality_check_err_type"] =
        qualityOk ? "" : (qualityErrorType.empty() ? ugripper::runtime::kErrorTypeUnknown : qualityErrorType);
    metadata["collection_duration_s"] = roundToOneDecimal(collectionDurationS);
    metadata["task_start_s"] = taskStartS_.value_or(0.0);
    metadata["task_stop_s"] = taskStopS_.value_or(0.0);
    const double bootTimeS =
        infoRoot.contains("boot_time_offset") && infoRoot["boot_time_offset"].is_number()
            ? infoRoot["boot_time_offset"].get<double>()
            : std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() -
                  std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    metadata["boot_time_s"] = bootTimeS;
    metadata["tactile_damage_sn_list"] = ordered_json::array();
    metadata["require_files"] = std::move(requiredFiles);
    metadata["video_details"] = std::move(videoDetails);

    if (!writeTextFileAtomically(episodePath / "metadata.json", metadata.dump(2) + "\n", errorMessage))
    {
        return false;
    }
    return true;
}

void ugripper::runtime::EpisodeManager::setTaskMarkers(std::optional<double> taskStartS,
                                                   std::optional<double> taskStopS)
{
    taskStartS_ = taskStartS;
    taskStopS_ = taskStopS;
}

std::string ugripper::runtime::EpisodeManager::finalizeEpisodeDir(const std::string &episodeDir,
                                                              std::string *errorMessage) const
{
    const fs::path sourcePath(episodeDir);
    const std::string sourceName = sourcePath.filename().string();
    const std::string finalName = stripEpisodeTempSuffix(sourceName);
    if (finalName == sourceName)
    {
        return episodeDir;
    }

    const fs::path targetPath = sourcePath.parent_path() / finalName;
    std::error_code error;
    if (fs::exists(targetPath, error))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "final episode directory already exists: " + targetPath.string();
        }
        return {};
    }

    fs::rename(sourcePath, targetPath, error);
    if (error)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "rename episode directory failed: " + error.message();
        }
        return {};
    }

    std::error_code removeError;
    fs::remove(episodeTimingPath(sourcePath), removeError);
    if (removeError)
    {
        DM_LOG_WARN("{}", (::DA::utils::LogString() << "failed to remove internal timing file: "
                      << episodeTimingPath(sourcePath)
                      << " error=" << removeError.message() << std::endl).str());
    }
    flushEpisodeDirectoriesToDisk(targetPath, "episode_finalize");
    return targetPath.string();
}

bool ugripper::runtime::EpisodeManager::writeFilteredCalibration(const std::string &episodeDir, std::string *errorMessage) const
{
    ordered_json mainImages = ordered_json::object();
    ordered_json tactileImages = ordered_json::object();
    ordered_json images = ordered_json::object();
    ordered_json imu = ordered_json::object();
    images["stereo_left"] = makeDefaultStereoCalibrationEntry();
    images["stereo_right"] = makeDefaultStereoCalibrationEntry();
    imu["imu_left"] = makeDefaultFaysImuCalibrationEntry();
    imu["imu_right"] = makeDefaultFaysImuCalibrationEntry();

    bool allMainCameraCalibrationValid = true;
    for (const auto &state : mainCameraRuntimeStates_)
    {
        if (!state.enabled)
        {
            continue;
        }
        std::string jsonPath;
        if (state.cameraName == "left_cam_main")
        {
            jsonPath = "cam_left_main";
        }
        else if (state.cameraName == "right_cam_main")
        {
            jsonPath = "cam_right_main";
        }
        else if (state.cameraName == "chest_cam_main")
        {
            jsonPath = "cam_chest_main";
        }
        if (jsonPath.empty())
        {
            continue;
        }

        mainImages[jsonPath] = makeDefaultMainCameraCalibrationEntry();
        std::string calibrationDetail;
        if (state.present &&
            isYuzhouMainCameraSn(state.serialNumber) &&
            state.calibrationPayloadCached &&
            validateYuzhouMainCameraCalibrationPayload(state.calibrationPayload, &calibrationDetail))
        {
            mainImages[jsonPath] = makeMainCameraCalibrationEntryFromPayload(state.calibrationPayload);
        }
        else
        {
            allMainCameraCalibrationValid = false;
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "main camera calibration cache unavailable for episode calibration: camera="
                << state.cameraName
                << " device=" << state.devicePath
                << " detail=" << (state.lastError.empty() ? calibrationDetail : state.lastError)
                << std::endl).str());
        }
    }

    bool allFaysCalibrationValid = true;
    if (stereoEnabled_)
    {
        for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
                 {"left", "left_stereo"},
                 {"right", "right_stereo"},
             })
        {
            json faysCalibration;
            std::string faysStatus;
            if (!loadFaysCalibrationFromStatus(stereoStatusFile_, entry.second, &faysCalibration, &faysStatus))
            {
                allFaysCalibrationValid = false;
                DM_LOG_WARN("{}", (::DA::utils::LogString() << "Fays calibration unavailable for "
                                     << entry.second << ": " << faysStatus << std::endl).str());
                continue;
            }

            images[entry.first == "left" ? "stereo_left" : "stereo_right"] =
                makeFaysStereoCalibrationEntry(faysCalibration);
            imu[entry.first == "left" ? "imu_left" : "imu_right"] =
                makeFaysImuCalibrationEntry(faysCalibration);
            DM_LOG_INFO("{}", (::DA::utils::LogString() << "Fays calibration loaded for "
                                << entry.second << ": " << faysStatus << std::endl).str());
        }
    }

    for (const auto &target : kTactileCalibrationTargets)
    {
        const auto *state = tactileCameraStateForName(target.cameraName);
        const std::string serial = state != nullptr ? state->serialNumber : std::string();
        if (serial.empty())
        {
            DM_LOG_WARN("{}", (::DA::utils::LogString()
                << "tactile serial cache unavailable for episode calibration: camera=" << target.cameraName
                << " device=" << target.devicePath
                << " detail=" << (state == nullptr
                                      ? "missing tactile serial cache state"
                                      : (state->lastError.empty() ? "empty tactile serial cache" : state->lastError))
                << "; write empty serial" << std::endl).str());
        }
        std::string imageKey = target.cameraName;
        if (imageKey.rfind("left_tcam_", 0) == 0)
        {
            imageKey = "tcam_left_" + imageKey.substr(std::string("left_tcam_").size());
        }
        else if (imageKey.rfind("right_tcam_", 0) == 0)
        {
            imageKey = "tcam_right_" + imageKey.substr(std::string("right_tcam_").size());
        }
        tactileImages[imageKey] = makeRuntimeTactileCalibrationEntry(serial);
    }

    ordered_json orderedImages = ordered_json::object();
    if (mainImages.contains("cam_chest_main"))
    {
        orderedImages["cam_chest_main"] = mainImages["cam_chest_main"];
    }
    orderedImages["stereo_left"] = images["stereo_left"];
    orderedImages["tcam_left_l"] = tactileImages.value("tcam_left_l", makeRuntimeTactileCalibrationEntry(""));
    orderedImages["tcam_left_r"] = tactileImages.value("tcam_left_r", makeRuntimeTactileCalibrationEntry(""));
    orderedImages["stereo_right"] = images["stereo_right"];
    orderedImages["tcam_right_l"] = tactileImages.value("tcam_right_l", makeRuntimeTactileCalibrationEntry(""));
    orderedImages["tcam_right_r"] = tactileImages.value("tcam_right_r", makeRuntimeTactileCalibrationEntry(""));
    orderedImages["cam_left_main"] = mainImages.value("cam_left_main", makeDefaultMainCameraCalibrationEntry());
    orderedImages["cam_right_main"] = mainImages.value("cam_right_main", makeDefaultMainCameraCalibrationEntry());

    ordered_json calibrationJson = ordered_json::object();
    const std::string calibrationStatus = (allMainCameraCalibrationValid && allFaysCalibrationValid)
                                             ? "calibrated"
                                             : "uncalibrated";
    calibrationJson["calibration_info"] = makeLockedCalibrationInfo(calibrationStatus);
    calibrationJson["observation"] = ordered_json::object();
    calibrationJson["observation"]["images"] = std::move(orderedImages);
    calibrationJson["observation"]["imu"] = std::move(imu);

    std::ofstream output(episodeDir + "/calibration.json");
    if (!output.is_open())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "cannot open calibration.json for write";
        }
        return false;
    }

    output << calibrationJson.dump(4) << "\n";
    return true;
}

bool ugripper::runtime::EpisodeManager::prepareEpisodeOutputs(const std::string &episodeDir, std::string *errorMessage) const
{
    (void)episodeDir;
    (void)errorMessage;
    return true;
}
