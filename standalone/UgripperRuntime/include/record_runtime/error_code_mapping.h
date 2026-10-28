#pragma once

#include <string_view>

namespace DA::Ugripper {

enum class ErrorCodeContext {
  Unknown,
  MainCamera,
  MainCameraStartup,
  MainCameraIo,
  MainCameraPipeline,
  MainCameraCalibration,
  TactileCamera,
  TactileCalibration,
  FaysDevice,
  FaysDaemon,
  FaysSdk,
  FaysFrame,
  FaysControl,
  Encoder,
  Hmi,
  HmiCalibration,
  CalibrationImport,
};

// The mapping key is error_type plus direct branch context, never message text.
inline std::string_view UgrCodeForErrorType(
    std::string_view error_type,
    ErrorCodeContext context = ErrorCodeContext::Unknown) {
  if (error_type == "gripper_version_mismatch") return "UGR-40-07";
  if (error_type == "disk_mount_lost") return "UGR-10-01";
  if (error_type == "encoder_init_failed") return "UGR-30-02";
  if (error_type == "encoder_stream_stale") return "UGR-30-07";
  if (error_type == "encoder_tail_missing") return "UGR-30-08";
  if (error_type == "disk_not_writable") return "UGR-10-02";
  if (error_type == "disk_full" || error_type == "disk_space_unavailable") return "UGR-10-03";
  if (error_type == "stereo_daemon_not_running" || error_type == "stereo_status_missing" ||
      error_type == "stereo_status_invalid") return "UGR-22-02";
  if (error_type == "stereo_control_failed") return "UGR-22-06";
  if (error_type == "hmi_all_disconnected" || error_type == "hmi_ports_disconnected" ||
      error_type == "hmi_input_disconnected") return "UGR-40-01";
  if (error_type == "hmi_input_inactive" || error_type == "hmi_ports_inactive") return "UGR-40-02";
  if (error_type == "ego_disconnected") return "UGR-82-01";
  if (error_type == "ego_time_sync_failed") return "UGR-82-02";
  if (error_type == "ego_pull_incomplete") return "UGR-82-04";
  if (error_type == "main_camera_v4l2_kernel_hang") return "UGR-20-03";
  if (error_type == "calibration_error") {
    if (context == ErrorCodeContext::MainCameraCalibration) return "UGR-20-06";
    if (context == ErrorCodeContext::TactileCalibration) return "UGR-21-03";
    if (context == ErrorCodeContext::FaysSdk) return "UGR-22-03";
    if (context == ErrorCodeContext::HmiCalibration) return "UGR-40-06";
    if (context == ErrorCodeContext::CalibrationImport) return "UGR-70-03";
  }
  if (error_type == "device_disconnected" || error_type == "critical_devices_missing") {
    if (context == ErrorCodeContext::MainCamera) return "UGR-20-01";
    if (context == ErrorCodeContext::TactileCamera) return "UGR-21-01";
    if (context == ErrorCodeContext::FaysDevice) return "UGR-22-01";
    if (context == ErrorCodeContext::Encoder) return "UGR-30-01";
    if (context == ErrorCodeContext::Hmi) return "UGR-40-01";
  }
  if (error_type == "stereo_frame_loss") return "UGR-22-04";
  if (error_type == "frame_loss") {
    if (context == ErrorCodeContext::MainCameraIo) return "UGR-20-03";
    if (context == ErrorCodeContext::FaysFrame) return "UGR-22-04";
  }
  if (error_type == "stereo_not_ready") {
    if (context == ErrorCodeContext::FaysDaemon) return "UGR-22-02";
    if (context == ErrorCodeContext::FaysSdk) return "UGR-22-03";
    if (context == ErrorCodeContext::FaysFrame) return "UGR-22-04";
    if (context == ErrorCodeContext::FaysControl) return "UGR-22-06";
  }
  if (error_type == "main_camera_v4l2_startup_failure") {
    if (context == ErrorCodeContext::MainCameraStartup) return "UGR-20-02";
    if (context == ErrorCodeContext::MainCameraIo) return "UGR-20-03";
    if (context == ErrorCodeContext::MainCameraPipeline) return "UGR-20-05";
  }
  if (error_type == "main_camera_short_stream" &&
      context == ErrorCodeContext::MainCameraIo) return "UGR-20-03";
  return "UGR-00-01";
}

}  // namespace DA::Ugripper
