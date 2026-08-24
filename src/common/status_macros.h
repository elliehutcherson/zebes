#pragma once

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Internal helper for concatenating macro values.
#define STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define STATUS_MACROS_CONCAT_NAME(x, y) STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define RETURN_IF_ERROR_IMPL(status, expr) \
  do {                                     \
    const absl::Status status = (expr);    \
    if (!status.ok()) return status;       \
  } while (0)

#define RETURN_IF_ERROR(expr) \
  RETURN_IF_ERROR_IMPL(STATUS_MACROS_CONCAT_NAME(status_macro_result_, __COUNTER__), expr)

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
