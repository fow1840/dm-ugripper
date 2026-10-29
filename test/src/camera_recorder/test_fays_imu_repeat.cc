#include <gtest/gtest.h>
#include "fays_stereo_recorder/imu_repeat_check.h"
using fays_stereo_recorder::ImuRepeatCheck;
TEST(FaysImuRepeat, TriggersOnFrame1000Once) {
    ImuRepeatCheck check;
    for (int i=1;i<1000;++i) EXPECT_FALSE(check.Observe({1,2,3,4,5,6}));
    EXPECT_TRUE(check.Observe({1,2,3,4,5,6}));
    for (int i=0;i<1000;++i) EXPECT_FALSE(check.Observe({1,2,3,4,5,6}));
}
TEST(FaysImuRepeat, EachAxisChangeResetsAndSidesAreIndependent) {
    for (int axis=0;axis<6;++axis) {
        ImuRepeatCheck left, right;
        std::array<double,6> values{1,2,3,4,5,6};
        for(int i=0;i<999;++i) EXPECT_FALSE(left.Observe(values));
        EXPECT_FALSE(right.Observe(values));
        values[axis]+=0.000001;
        for(int i=0;i<999;++i) EXPECT_FALSE(left.Observe(values));
        EXPECT_TRUE(left.Observe(values));
        EXPECT_FALSE(right.Observe(values));
    }
}
