#include "record_runtime/error_code_mapping.h"

#include <gtest/gtest.h>

using ugripper::runtime::ErrorCodeContext;
using ugripper::runtime::UgrCodeForErrorType;

TEST(ErrorCodeMapping, MapsUniqueLegacyTypesWithoutPayloadInspection) {
  EXPECT_EQ(UgrCodeForErrorType("disk_mount_lost"), "UGR-10-01");
  EXPECT_EQ(UgrCodeForErrorType("disk_not_writable"), "UGR-10-02");
  EXPECT_EQ(UgrCodeForErrorType("disk_full"), "UGR-10-03");
  EXPECT_EQ(UgrCodeForErrorType("stereo_daemon_not_running"), "UGR-22-02");
  EXPECT_EQ(UgrCodeForErrorType("stereo_control_failed"), "UGR-22-06");
  EXPECT_EQ(UgrCodeForErrorType("hmi_all_disconnected"), "UGR-40-01");
  EXPECT_EQ(UgrCodeForErrorType("hmi_input_inactive"), "UGR-40-02");
  EXPECT_EQ(UgrCodeForErrorType("ego_disconnected"), "UGR-82-01");
  EXPECT_EQ(UgrCodeForErrorType("ego_pull_incomplete"), "UGR-82-04");
}

TEST(ErrorCodeMapping, RequiresDirectContextForAmbiguousLegacyTypes) {
  EXPECT_EQ(UgrCodeForErrorType("device_disconnected", ErrorCodeContext::MainCamera), "UGR-20-01");
  EXPECT_EQ(UgrCodeForErrorType("device_disconnected", ErrorCodeContext::TactileCamera), "UGR-21-01");
  EXPECT_EQ(UgrCodeForErrorType("device_disconnected", ErrorCodeContext::FaysDevice), "UGR-22-01");
  EXPECT_EQ(UgrCodeForErrorType("device_disconnected", ErrorCodeContext::Encoder), "UGR-30-01");
  EXPECT_EQ(UgrCodeForErrorType("device_disconnected", ErrorCodeContext::Hmi), "UGR-40-01");
  EXPECT_EQ(UgrCodeForErrorType("frame_loss", ErrorCodeContext::MainCameraIo), "UGR-20-03");
  EXPECT_EQ(UgrCodeForErrorType("frame_loss", ErrorCodeContext::FaysFrame), "UGR-22-04");
  EXPECT_EQ(UgrCodeForErrorType("calibration_error", ErrorCodeContext::CalibrationImport), "UGR-70-03");
}

TEST(ErrorCodeMapping, UnknownContextUsesOnlyTheDocumentedFallback) {
  EXPECT_EQ(UgrCodeForErrorType("device_disconnected"), "UGR-00-01");
  EXPECT_EQ(UgrCodeForErrorType("missing_file"), "UGR-00-01");
  EXPECT_EQ(UgrCodeForErrorType("new_unclassified_type"), "UGR-00-01");
}
