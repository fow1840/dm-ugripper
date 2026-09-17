#include <gtest/gtest.h>
#include "fays_stereo_recorder/gray_nv12.h"

using fays_stereo_recorder::GrayToNv12;

TEST(FaysGrayNv12, MapsGrayLevelsAndWritesNeutralInterleavedChroma) {
    const uint8_t gray[] = {0, 1, 64, 128, 192, 254, 255, 0};
    std::vector<uint8_t> nv12;
    ASSERT_TRUE(GrayToNv12(gray, sizeof(gray), 4, 2, nv12));
    const std::vector<uint8_t> expected = {16, 17, 71, 126, 181, 234, 235, 16,
                                            128, 128, 128, 128};
    EXPECT_EQ(nv12, expected);
}

TEST(FaysGrayNv12, ChromaIsResetWhenResolutionShrinksIntoOldLuma) {
    std::vector<uint8_t> gray(32, 255), nv12;
    ASSERT_TRUE(GrayToNv12(gray.data(), gray.size(), 8, 4, nv12));
    ASSERT_TRUE(GrayToNv12(gray.data(), gray.size(), 4, 2, nv12));
    ASSERT_EQ(nv12.size(), 12u);
    for (size_t i = 0; i < 8; ++i) EXPECT_EQ(nv12[i], 235);
    for (size_t i = 8; i < 12; ++i) EXPECT_EQ(nv12[i], 128);
}

TEST(FaysGrayNv12, RejectsUnsupportedLayoutsWithoutChangingOutput) {
    uint8_t gray[8]{};
    std::vector<uint8_t> nv12{42};
    EXPECT_FALSE(GrayToNv12(gray, 7, 4, 2, nv12));
    EXPECT_FALSE(GrayToNv12(gray, 8, 3, 2, nv12));
    EXPECT_FALSE(GrayToNv12(gray, 8, 4, 1, nv12));
    EXPECT_FALSE(GrayToNv12(gray, 8, -4, 2, nv12));
    EXPECT_FALSE(GrayToNv12(nullptr, 8, 4, 2, nv12));
    EXPECT_EQ(nv12, std::vector<uint8_t>{42});
}
