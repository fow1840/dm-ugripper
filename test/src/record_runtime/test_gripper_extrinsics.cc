#include "record_runtime/gripper_extrinsics.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
using DA::Ugripper::ReadGripperTransform;
using Payload = std::array<uint8_t, 1024>;

void Put32(Payload &payload, size_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) payload[offset + i] = (value >> (8 * i)) & 0xff;
}

Payload ValidPayload()
{
    Payload payload{};
    std::memcpy(payload.data(), "UCAL", 4);
    Put32(payload, 4, 0x00030000);
    payload[8] = 0x30;
    payload[9] = 2;
    payload[10] = 32;
    payload[12] = 0xc0;
    for (size_t offset : {0x1b0, 0x1f0})
        for (size_t i = 0; i < 16; ++i)
        {
            const float value = i % 5 == 0 ? 1.0f : i == 3 ? -0.04f : 0.0f;
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            Put32(payload, offset + 4 * i, bits);
        }
    return payload;
}

TEST(GripperExtrinsics, StoredTranslationIsNotInverted)
{
    auto payload = ValidPayload();
    nlohmann::json matrix;
    ASSERT_TRUE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    EXPECT_DOUBLE_EQ(matrix[0][3].get<double>(), double(-0.04f));
    EXPECT_TRUE(ReadGripperTransform(payload, 0x00030000, 7, 0x1f0, &matrix));
}

TEST(GripperExtrinsics, RejectsMissingTruncatedAndUnsupportedData)
{
    nlohmann::json matrix;
    auto payload = ValidPayload();
    payload[12] = 0x40;
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00030000, 7, 0x1f0, &matrix));
    payload[8] = 0xef;
    payload[9] = 1;
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    payload = ValidPayload();
    Put32(payload, 4, 0x00040000);
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    payload = {};
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    payload.fill(0xff);
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
}

TEST(GripperExtrinsics, RejectsNonfiniteAndInvalidHomogeneousTransforms)
{
    nlohmann::json matrix;
    auto payload = ValidPayload();
    Put32(payload, 0x1b0, 0x7fc00000);
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    payload = ValidPayload();
    Put32(payload, 0x1b0 + 60, 0);
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    payload = ValidPayload();
    Put32(payload, 0x1b0, 0);
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
}

TEST(GripperExtrinsics, V1DoesNotClaimV2Transform)
{
    auto payload = ValidPayload();
    Put32(payload, 4, 0x00010000);
    nlohmann::json matrix;
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    Put32(payload, 4, 0x00020000);
    EXPECT_TRUE(ReadGripperTransform(payload, 0x00020000, 6, 0x1b0, &matrix));
    EXPECT_FALSE(ReadGripperTransform(payload, 0x00030000, 7, 0x1f0, &matrix));
}
}  // namespace
