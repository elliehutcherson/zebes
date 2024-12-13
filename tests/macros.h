#include "absl/status/statusor.h"

namespace {

template <typename T>
using StatusOr = absl::StatusOr<T>;

}  // namespace

#define ASSERT_OK_AND_ASSIGN(lhs, rexpr)                                \
  ASSERT_OK_AND_ASSIGN_IMPL(                                            \
      STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs,    \
      rexpr);

#define ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr)     \
  auto statusor = (rexpr);                                  \
  ASSERT_TRUE(statusor.status().ok()) << statusor.status(); \
  lhs = std::move(statusor).value()

#define STATUS_MACROS_CONCAT_NAME(x, y) STATUS_MACROS_CONCAT_IMPL(x, y)
#define STATUS_MACROS_CONCAT_IMPL(x, y) x##y