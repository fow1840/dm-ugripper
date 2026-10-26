#pragma once
#include "utils/stream_loss_monitor.h"
#include "utils/ugripper_logger.h"
#include <string>

namespace DA::utils {
inline void LogStreamLossSummary(const StreamLossMonitor& monitor,
                                 const std::string& stream, const char* basis) {
    DM_LOG_INFO("[stream_loss] summary stream={} basis={} received={} missing={} loss_ratio={} warning_latched={} error_latched={}",
                stream, basis, monitor.Received(), monitor.Missing(), monitor.Rate(), monitor.Warned(), monitor.Failed());
}
inline bool ReportStreamLoss(StreamLossMonitor& monitor, bool camera,
                             const std::string& stream,
                             const char* basis, const char* error_code) {
    const auto event = monitor.Check(camera);
    if (event.warning)
        DM_LOG_WARN("[stream_loss] stream={} basis={} missing={} received={} loss_ratio={} threshold=missing>10", stream, basis, monitor.Missing(), monitor.Received(), monitor.Rate());
    if (event.error)
        DM_LOG_ERROR_CODE(error_code, "[stream_loss] stream={} basis={} missing={} received={} loss_ratio={} threshold=loss_ratio>0.02", stream, basis, monitor.Missing(), monitor.Received(), monitor.Rate());
    return event.error;
}
}
