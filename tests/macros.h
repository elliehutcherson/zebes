#pragma once

#include "absl/status/statusor.h"
#include "gtest/gtest-matchers.h"
#include "gtest/gtest.h"

namespace zebes::test_internal {

// The printable status behind either kind of value.
//
// absl::StatusOr<T> has no operator<< unless T does, and a failing assertion
// wants the error rather than the value it did not produce.
inline const absl::Status& StatusForMessage(const absl::Status& status) { return status; }

template <typename T>
const absl::Status& StatusForMessage(const absl::StatusOr<T>& status_or) {
  return status_or.status();
}

}  // namespace zebes::test_internal

// Assert that a Status or StatusOr is ok, reporting the error when it is not.
//
// The expression is evaluated exactly once. Streaming it a second time for the
// failure message -- which is what these used to do -- re-runs whatever produced
// the status, and gtest only evaluates that stream on failure, so the call
// happened twice at precisely the moment the message needed to be trustworthy.
// `ASSERT_OK(manager_->CreateLevel(std::move(level)))` moved from an
// already-moved value to build its own error text.
#define EXPECT_OK(expression) \
  EXPECT_OK_IMPL(ZEBES_TEST_STATUS_MACROS_CONCAT_NAME(_status_value, __COUNTER__), expression)

#define EXPECT_OK_IMPL(status, expression) \
  const auto& status = (expression);       \
  EXPECT_TRUE(status.ok()) << ::zebes::test_internal::StatusForMessage(status)

#define ASSERT_OK(expression) \
  ASSERT_OK_IMPL(ZEBES_TEST_STATUS_MACROS_CONCAT_NAME(_status_value, __COUNTER__), expression)

#define ASSERT_OK_IMPL(status, expression) \
  const auto& status = (expression);       \
  ASSERT_TRUE(status.ok()) << ::zebes::test_internal::StatusForMessage(status)

#define ASSERT_OK_AND_ASSIGN(lhs, rexpr)                                                         \
  ASSERT_OK_AND_ASSIGN_IMPL(ZEBES_TEST_STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), \
                            lhs, rexpr);

#define ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr)     \
  auto statusor = (rexpr);                                  \
  ASSERT_TRUE(statusor.status().ok()) << statusor.status(); \
  lhs = std::move(statusor).value()

#define ZEBES_TEST_STATUS_MACROS_CONCAT_NAME(x, y) ZEBES_TEST_STATUS_MACROS_CONCAT_IMPL(x, y)
#define ZEBES_TEST_STATUS_MACROS_CONCAT_IMPL(x, y) x##y
