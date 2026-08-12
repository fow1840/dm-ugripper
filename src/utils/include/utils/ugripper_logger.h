#pragma once

#include <logger.h>

#include "error_code.h"
#include "log_string.h"

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#if __cplusplus >= 202002L
#  include <source_location>
#endif

namespace DA::utils {

#if __cplusplus >= 202002L
template <typename... Args>
inline void LogUgrErrorCode(bool critical, const std::source_location& location,
                            std::string_view code, fmt::format_string<Args...> format,
                            Args&&... args) {
  const auto lines = AppendUgrErrorCodeToPhysicalLines(
      fmt::format(format, std::forward<Args>(args)...), code);
  for (const auto& line : lines) {
    if (critical) {
      SpdLogger::GetInstance().critical(location, "{}", line);
    } else {
      SpdLogger::GetInstance().error(location, "{}", line);
    }
  }
}
#else
template <typename... Args>
inline void LogUgrErrorCode(bool critical, const char* file, int line,
                            const char* function, std::string_view code,
                            const char* format, Args&&... args) {
  const auto lines = AppendUgrErrorCodeToPhysicalLines(
      fmt::format(fmt::runtime(format), std::forward<Args>(args)...), code);
  for (const auto& physical_line : lines) {
    if (critical) {
      SpdLogger::GetInstance().critical(file, line, function, "{}", physical_line);
    } else {
      SpdLogger::GetInstance().error(file, line, function, "{}", physical_line);
    }
  }
}
#endif

template <typename... Args>
inline void InitializeUgripperLogger(Args&&... args) noexcept {
  try {
    SpdLogger::init(std::forward<Args>(args)...);
  } catch (const std::exception& exception) {
    std::cerr << "Log initialization failed: " << exception.what()
              << " error_code=UGR-00-01\n";
  } catch (...) {
    std::cerr << "Log initialization failed: unknown error"
              << " error_code=UGR-00-01\n";
  }
}

}  // namespace DA::utils

#undef DM_LOG_INIT
#define DM_LOG_INIT(...) ::DA::utils::InitializeUgripperLogger(__VA_ARGS__)
#if __cplusplus >= 202002L
#  define DM_LOG_ERROR_CODE(code, ...) \
    ::DA::utils::LogUgrErrorCode(false, std::source_location::current(), (code), __VA_ARGS__)
#  define DM_LOG_CRITICAL_CODE(code, ...) \
    ::DA::utils::LogUgrErrorCode(true, std::source_location::current(), (code), __VA_ARGS__)
#else
#  define DM_LOG_ERROR_CODE(code, ...) \
    ::DA::utils::LogUgrErrorCode(false, __FILE__, __LINE__, __func__, (code), __VA_ARGS__)
#  define DM_LOG_CRITICAL_CODE(code, ...) \
    ::DA::utils::LogUgrErrorCode(true, __FILE__, __LINE__, __func__, (code), __VA_ARGS__)
#endif
