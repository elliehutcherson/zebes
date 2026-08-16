#pragma once

#include <chrono>
#include <exception>
#include <future>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {

// One typed unit of CPU work running outside the caller's thread.
//
// Subsystems submit functions that return StatusOr and poll the task from their
// owning thread. The implementation is the repository's exception boundary for
// the standard threading library: submission failures and exceptions escaping
// a worker are translated to Status, so callers never manage a future or use
// try/catch.
//
// Destroying an unfinished task waits for its worker. Work should therefore be
// bounded and should not depend on the owning thread making further progress.
template <typename Result>
class BackgroundTask {
 public:
  static_assert(std::is_move_constructible_v<Result>,
                "A background task result must be move constructible");

  ~BackgroundTask() = default;

  BackgroundTask(BackgroundTask&& other) noexcept = default;
  BackgroundTask& operator=(BackgroundTask&& other) noexcept = default;

  BackgroundTask(const BackgroundTask&) = delete;
  BackgroundTask& operator=(const BackgroundTask&) = delete;

  template <typename Work>
  static absl::StatusOr<BackgroundTask> Start(Work&& work) {
    static_assert(std::is_invocable_r_v<TaskResult, std::decay_t<Work>&&>,
                  "Background task work must return StatusOr<Result>");

    try {
      auto result = std::async(
          std::launch::async,
          [work = std::forward<Work>(work)]() mutable -> TaskResult { return std::move(work)(); });
      return BackgroundTask(Handle(std::move(result)));
    } catch (const std::exception& error) {
      return absl::InternalError(absl::StrCat("Could not start background task: ", error.what()));
    } catch (...) {
      return absl::InternalError("Could not start background task: unknown exception");
    }
  }

  // Non-blocking readiness check. A moved-from or already-consumed task is a
  // failed precondition rather than an idle task.
  absl::StatusOr<bool> IsReady() const {
    if (!handle_.result.valid()) {
      return absl::FailedPreconditionError("Background task has no pending result");
    }
    return handle_.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  // Blocks without consuming the result. Primarily useful at controlled
  // shutdown boundaries and in tests.
  absl::Status Wait() const {
    if (!handle_.result.valid()) {
      return absl::FailedPreconditionError("Background task has no pending result");
    }
    handle_.result.wait();
    return absl::OkStatus();
  }

  absl::StatusOr<Result> TakeResult() {
    if (!handle_.result.valid()) {
      return absl::FailedPreconditionError("Background task has no pending result");
    }
    try {
      ASSIGN_OR_RETURN(Result result, handle_.result.get());
      return result;
    } catch (const std::exception& error) {
      return absl::InternalError(
          absl::StrCat("Background task failed outside its status contract: ", error.what()));
    } catch (...) {
      return absl::InternalError(
          "Background task failed outside its status contract: unknown exception");
    }
  }

 private:
  using TaskResult = absl::StatusOr<Result>;

  struct Handle {
    explicit Handle(std::future<TaskResult> result) : result(std::move(result)) {}

    std::future<TaskResult> result;
  };

  explicit BackgroundTask(Handle handle) : handle_(std::move(handle)) {}

  Handle handle_;
};

}  // namespace zebes
