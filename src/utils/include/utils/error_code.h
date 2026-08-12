#pragma once

#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace DA::utils {

inline bool IsValidUgrErrorCode(std::string_view code) {
  return code.size() == 9 && code.substr(0, 4) == "UGR-" &&
         code[4] >= '0' && code[4] <= '9' && code[5] >= '0' && code[5] <= '9' &&
         code[6] == '-' && code[7] >= '0' && code[7] <= '9' &&
         code[8] >= '0' && code[8] <= '9';
}

inline std::string AppendUgrErrorCodeToLines(std::string_view payload,
                                             std::string_view code) {
  if (!IsValidUgrErrorCode(code)) {
    throw std::invalid_argument("invalid Ugripper error code");
  }
  const std::string normalized_code(code);
  const std::string suffix = " error_code=" + normalized_code;
  std::string result;
  result.reserve(payload.size() + suffix.size());

  size_t begin = 0;
  while (begin <= payload.size()) {
    const size_t end = payload.find('\n', begin);
    std::string_view line = payload.substr(
        begin, end == std::string_view::npos ? payload.size() - begin : end - begin);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (!line.empty()) {
      result.append(line);
      const size_t field_pos = line.find("error_code=");
      if (field_pos == std::string_view::npos) {
        result.append(suffix);
      } else {
        constexpr std::string_view kField = "error_code=";
        const std::string_view existing = line.substr(field_pos + kField.size());
        const bool is_single_trailing_field =
            field_pos > 0 && line[field_pos - 1] == ' ' &&
            line.find(kField, field_pos + kField.size()) == std::string_view::npos &&
            IsValidUgrErrorCode(existing);
        if (!is_single_trailing_field || existing != code) {
          throw std::invalid_argument("payload contains a conflicting Ugripper error code");
        }
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    result.push_back('\n');
    begin = end + 1;
  }
  return result;
}

inline std::vector<std::string> AppendUgrErrorCodeToPhysicalLines(
    std::string_view payload, std::string_view code) {
  const std::string normalized = AppendUgrErrorCodeToLines(payload, code);
  std::vector<std::string> lines;
  size_t begin = 0;
  while (begin <= normalized.size()) {
    const size_t end = normalized.find('\n', begin);
    const std::string_view line = std::string_view(normalized).substr(
        begin, end == std::string::npos ? normalized.size() - begin : end - begin);
    if (!line.empty()) {
      lines.emplace_back(line);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return lines;
}

}  // namespace DA::utils
