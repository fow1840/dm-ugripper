#pragma once
#include "utils/stream_loss_monitor.h"
#include "utils/stereo_frame_loss_state.h"
#include "utils/ugripper_logger.h"
#include <fstream>
#include <string>

namespace DA::utils {
// Runtime-only IPC: no new episode files or metadata fields. A pending result
// prevents a failed final write from silently passing QC. Each stream owns a file.
inline bool WriteStreamLossResult(const std::filesystem::path& episode,
                                  const std::string& stream,
                                  const StreamLossMonitor& monitor, bool finalized) {
    const auto path = StreamLossResultPath(episode, stream);
    const auto temp = path.string() + ".tmp";
    std::ofstream out(temp, std::ios::trunc);
    out << "{\"finalized\":" << (finalized ? "true" : "false")
        << ",\"received\":" << monitor.Received()
        << ",\"missing\":" << monitor.Missing()
        << ",\"rate_exceeded\":" << (monitor.FinalRateExceeded() ? "true" : "false") << "}\n";
    out.close();
    std::error_code error;
    if (out) std::filesystem::rename(temp, path, error);
    if (!out || error) {
        DM_LOG_ERROR_CODE("UGR-00-01", "[stream_loss] result write failed stream={}", stream);
        return false;
    }
    return true;
}
inline bool FinalizeStreamLoss(const StreamLossMonitor& monitor,
                               const std::filesystem::path& episode,
                               const std::string& stream, const char* basis,
                               const char* error_code) {
    const bool exceeded = monitor.FinalRateExceeded();
    if (exceeded)
        DM_LOG_ERROR_CODE(error_code, "[stream_loss] final stream={} received={} missing={} loss_ratio={} max_consecutive={} basis={} threshold=loss_ratio>0.02",
            stream, monitor.Received(), monitor.Missing(), monitor.Rate(), monitor.MaxConsecutive(), basis);
    else
        DM_LOG_INFO("[stream_loss] final stream={} received={} missing={} loss_ratio={} max_consecutive={} basis={}",
            stream, monitor.Received(), monitor.Missing(), monitor.Rate(), monitor.MaxConsecutive(), basis);
    return WriteStreamLossResult(episode, stream, monitor, true) && !exceeded;
}
inline bool ReportStreamLoss(StreamLossMonitor& monitor, bool camera,
                             const std::string& stream, const char* basis,
                             const char* error_code,
                             uint64_t max_allowed_consecutive = UINT64_MAX) {
    const auto event = monitor.Check(camera, max_allowed_consecutive);
    if (event.error)
        DM_LOG_ERROR_CODE(error_code, "[stream_loss] stream={} consecutive_missing={} threshold=>{} basis={}",
            stream, monitor.MaxConsecutive(), max_allowed_consecutive, basis);
    return event.error;
}
}
