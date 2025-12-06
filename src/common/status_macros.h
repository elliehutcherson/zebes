#pragma once

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#define RETURN_IF_ERROR(expr)                                                \
  do {                                                                       \
    /* Using _status below to avoid capture problems if expr is "status". */ \
    const absl::Status _status = (expr);                                     \
    if (!_status.ok()) return _status;                                       \
  } while (0)

// Internal helper for concatenating macro values.
#define STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define STATUS_MACROS_CONCAT_NAME(x, y) STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                          \
  if (!statusor.ok()) return statusor.status();     \
  lhs = std::move(*statusor);

#define ASSIGN_OR_RETURN(lhs, rexpr) \
  ASSIGN_OR_RETURN_IMPL(STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, rexpr);

#define LOG_IF_ERROR(expr)               \
  do {                                   \
    const absl::Status _status = (expr); \
    if (!_status.ok()) {                 \
      LOG(ERROR) << _status;             \
    }                                    \
  } while (0)
