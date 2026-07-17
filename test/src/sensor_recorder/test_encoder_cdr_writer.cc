#include "episode_mcap/ros2_cdr_writer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

template <typename T>
T ReadAt(const std::vector<std::byte>& data, size_t offset) {
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

TEST(EncoderCdrWriter, EncodesRos2CdrLayoutAndValues) {
    constexpr uint64_t kStampNs = 1700000000123456789ULL;
    constexpr int32_t kRaw = -123456789;
    constexpr float kRad = 1.25F;

    const auto data = ugripper::episode_mcap::EncodeEncoder(kStampNs, kRaw, kRad);

    ASSERT_EQ(data.size(), 28U);
    EXPECT_EQ(data[0], std::byte{0x00});
    EXPECT_EQ(data[1], std::byte{0x01});
    EXPECT_EQ(data[2], std::byte{0x00});
    EXPECT_EQ(data[3], std::byte{0x00});
    EXPECT_EQ(ReadAt<int32_t>(data, 4), 1700000000);
    EXPECT_EQ(ReadAt<uint32_t>(data, 8), 123456789U);
    EXPECT_EQ(ReadAt<uint32_t>(data, 12), 1U);
    EXPECT_EQ(data[16], std::byte{0x00});
    EXPECT_EQ(ReadAt<int32_t>(data, 20), kRaw);
    EXPECT_FLOAT_EQ(ReadAt<float>(data, 24), kRad);
}

TEST(EncoderCdrWriter, PublishesRos2MessageDefinition) {
    const std::string schema = ugripper::episode_mcap::EncoderSchema();
    EXPECT_NE(schema.find("std_msgs/Header header"), std::string::npos);
    EXPECT_NE(schema.find("builtin_interfaces/Time stamp"), std::string::npos);
    EXPECT_NE(schema.find("int32 raw"), std::string::npos);
    EXPECT_NE(schema.find("float32 rad"), std::string::npos);
}

TEST(StringCdrWriter, EncodesRos2StringLayoutAndValues) {
    const std::string value = R"({"quality_check_status":"success"})";

    const auto data = ugripper::episode_mcap::EncodeString(value);

    ASSERT_EQ(data.size(), 4U + 4U + value.size() + 1U);
    EXPECT_EQ(data[0], std::byte{0x00});
    EXPECT_EQ(data[1], std::byte{0x01});
    EXPECT_EQ(data[2], std::byte{0x00});
    EXPECT_EQ(data[3], std::byte{0x00});
    EXPECT_EQ(ReadAt<uint32_t>(data, 4), value.size() + 1U);
    EXPECT_EQ(std::memcmp(data.data() + 8, value.data(), value.size()), 0);
    EXPECT_EQ(data.back(), std::byte{0x00});
}

TEST(StringCdrWriter, PublishesStandardRos2Schema) {
    EXPECT_STREQ(ugripper::episode_mcap::StringSchema(), "string data");
}

}  // namespace
