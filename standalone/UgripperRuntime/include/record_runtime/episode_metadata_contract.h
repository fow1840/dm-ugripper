#pragma once

#include "record_runtime/episode_types.h"
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

namespace DA::Ugripper {

inline bool IsKnownDeviceVersion(const std::string &version)
{
    return !version.empty() && version.find_first_not_of(" \t\r\n") != std::string::npos &&
           version != "unknown" && version != "disabled";
}

inline std::vector<std::string> MissingRequiredDeviceVersions(
    const DeviceVersions &versions, bool chestEnabled, bool stereoEnabled, bool egoBound)
{
    std::vector<std::string> missing;
    const auto check = [&](const char *field) {
        const auto value = versions.find(field);
        if (value == versions.end() || !IsKnownDeviceVersion(value->second)) missing.emplace_back(field);
    };
    for (const auto *field : {"hardware_version", "software_version", "das_usb_updater_version"}) check(field);
    for (const auto *field : kDeviceVersionFields)
    {
        const std::string_view name(field);
        if ((!chestEnabled && name == "cam_chest_firmware_version") ||
            (!stereoEnabled && name.starts_with("stereo_")) ||
            (!egoBound && name.starts_with("ego_"))) continue;
        check(field);
    }
    return missing;
}

inline bool ApplyRelatedEpisodeMetadata(nlohmann::ordered_json *ego,
                                         const nlohmann::ordered_json &parent,
                                         std::string *error)
{
    if (ego == nullptr || !ego->is_object() || !parent.is_object() ||
        !parent.contains("episode_name") || !parent["episode_name"].is_string() ||
        parent["episode_name"].get<std::string>().empty() ||
        !parent.contains("data_uuid") || !parent["data_uuid"].is_string() ||
        parent["data_uuid"].get<std::string>().empty())
    {
        if (error != nullptr) *error = "invalid ego/parent metadata for episode linkage";
        return false;
    }
    (*ego)["related_episode_name"] = parent["episode_name"];
    (*ego)["related_episode_uuid"] = parent["data_uuid"];
    return true;
}

}  // namespace DA::Ugripper
