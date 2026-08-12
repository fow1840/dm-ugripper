#include "utils/error_code.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

TEST(LoggingV3, ValidatesAndAppendsErrorCodePerPhysicalLine) {
  EXPECT_TRUE(DA::utils::IsValidUgrErrorCode("UGR-22-06"));
  EXPECT_FALSE(DA::utils::IsValidUgrErrorCode("UGR-2-6"));
  EXPECT_THROW(DA::utils::AppendUgrErrorCodeToLines("failed", "UGR-2-6"),
               std::invalid_argument);
  EXPECT_EQ(DA::utils::AppendUgrErrorCodeToLines("first\nsecond", "UGR-22-06"),
            "first error_code=UGR-22-06\nsecond error_code=UGR-22-06");
  EXPECT_EQ(DA::utils::AppendUgrErrorCodeToLines(
                "failed error_code=UGR-22-06", "UGR-22-06"),
            "failed error_code=UGR-22-06");
  EXPECT_THROW(DA::utils::AppendUgrErrorCodeToLines(
                   "failed error_code=UGR-00-01", "UGR-22-06"),
               std::invalid_argument);
  EXPECT_THROW(DA::utils::AppendUgrErrorCodeToLines(
                   "failed error_code=invalid", "UGR-22-06"),
               std::invalid_argument);
}

TEST(LoggingV3, SplitsCodedPayloadBeforeSubmittingToPpMainLogger) {
  EXPECT_EQ(DA::utils::AppendUgrErrorCodeToPhysicalLines(
                "first\nsecond\n", "UGR-22-06"),
            (std::vector<std::string>{"first error_code=UGR-22-06",
                                      "second error_code=UGR-22-06"}));
}

}  // namespace
