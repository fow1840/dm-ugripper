#include "record_runtime/episode_metadata_contract.h"
#include <gtest/gtest.h>
using namespace DA::Ugripper;

static DeviceVersions KnownVersions()
{
    DeviceVersions versions;
    for (const auto *key : kDeviceVersionFields) versions[key] = "1.2.3";
    for (const auto *key : {"hardware_version", "software_version", "das_usb_updater_version"}) versions[key] = "1.2.3";
    return versions;
}

TEST(EpisodeMetadataContract, MissingVersionsAndEnabledDeviceExemptions)
{
    auto versions = KnownVersions();
    EXPECT_TRUE(MissingRequiredDeviceVersions(versions, true, true, true).empty());
    for (const auto *key : kDeviceVersionFields) {
        auto incomplete = versions;
        incomplete[key] = "";
        EXPECT_EQ(MissingRequiredDeviceVersions(incomplete, true, true, true), std::vector<std::string>{key});
    }
    versions["cam_chest_firmware_version"] = "disabled";
    versions["stereo_left_firmware_version"] = "disabled";
    versions["stereo_right_firmware_version"] = "disabled";
    versions["ego_software_version"] = "";
    versions["ego_firmware_version"] = "";
    EXPECT_TRUE(MissingRequiredDeviceVersions(versions, false, false, false).empty());
    EXPECT_EQ(MissingRequiredDeviceVersions(versions, false, false, true).size(), 2u);
    for (const auto &value : {"", " \t", "unknown", "disabled"}) {
        versions["gripper_left_firmware_version"] = value;
        EXPECT_EQ(MissingRequiredDeviceVersions(versions, false, false, false),
                  std::vector<std::string>{"gripper_left_firmware_version"});
    }
}

TEST(EpisodeMetadataContract, LinkagePreservesEgoIdentityOrderAndIsIdempotent)
{
    nlohmann::ordered_json ego = {{"device_sn", "ego-sn"}, {"episode_name", "ego-episode"},
                                  {"data_uuid", "ego-uuid"}, {"nested", {{"keep", 2}}}};
    const auto original = ego;
    const nlohmann::ordered_json parent = {{"episode_name", "episode_20261012_0001"}, {"data_uuid", "umi-uuid"}};
    std::string error;
    ASSERT_TRUE(ApplyRelatedEpisodeMetadata(&ego, parent, &error));
    EXPECT_EQ(ego["related_episode_name"], parent["episode_name"]);
    EXPECT_EQ(ego["related_episode_uuid"], parent["data_uuid"]);
    const auto linked = ego;
    ASSERT_TRUE(ApplyRelatedEpisodeMetadata(&ego, parent, &error));
    EXPECT_EQ(ego.dump(), linked.dump());
    ego.erase("related_episode_name"); ego.erase("related_episode_uuid");
    EXPECT_EQ(ego.dump(), original.dump());
}

TEST(EpisodeMetadataContract, MalformedMetadataDoesNotMutateEgo)
{
    nlohmann::ordered_json ego = {{"keep", true}};
    const auto original = ego;
    std::string error;
    for (const nlohmann::ordered_json parent : {nlohmann::ordered_json::object(),
           nlohmann::ordered_json{{"episode_name", "episode"}, {"data_uuid", 42}},
           nlohmann::ordered_json{{"episode_name", ""}, {"data_uuid", "uuid"}}}) {
        EXPECT_FALSE(ApplyRelatedEpisodeMetadata(&ego, parent, &error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(ego, original);
    }
    auto array = nlohmann::ordered_json::array();
    EXPECT_FALSE(ApplyRelatedEpisodeMetadata(&array, {{"episode_name", "episode"}, {"data_uuid", "uuid"}}, &error));
}
