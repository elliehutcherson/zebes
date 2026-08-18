#include "common/blocking_callback_thread.h"

#include <exception>
#include <memory>
#include <thread>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace zebes {

BlockingCallbackThread::~BlockingCallbackThread() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

absl::StatusOr<BlockingCallbackThread> BlockingCallbackThread::Start(Callback callback) {
  if (!callback) {
    return absl::InvalidArgumentError("Blocking thread callback is empty");
  }
  try {
    std::unique_ptr<State> state = std::make_unique<State>();
    State* const state_pointer = state.get();
    std::thread thread([state_pointer, callback = std::move(callback)]() mutable {
      try {
        state_pointer->result = callback();
      } catch (const std::exception& error) {
        state_pointer->result = absl::InternalError(
            absl::StrCat("Blocking thread callback threw an exception: ", error.what()));
      } catch (...) {
        state_pointer->result =
            absl::InternalError("Blocking thread callback threw an unknown exception");
      }
    });
    return BlockingCallbackThread(std::move(state), std::move(thread));
  } catch (const std::exception& error) {
    return absl::InternalError(absl::StrCat("Could not start blocking thread: ", error.what()));
  } catch (...) {
    return absl::InternalError("Could not start blocking thread: unknown exception");
  }
}

absl::Status BlockingCallbackThread::Wait() {
  if (!state_) {
    return absl::FailedPreconditionError("Blocking thread has no callback");
  }
  if (thread_.joinable()) {
    try {
      thread_.join();
    } catch (const std::exception& error) {
      return absl::InternalError(absl::StrCat("Could not join blocking thread: ", error.what()));
    } catch (...) {
      return absl::InternalError("Could not join blocking thread: unknown exception");
    }
  }
  return state_->result;
}

BlockingCallbackThread::BlockingCallbackThread(std::unique_ptr<State> state, std::thread thread)
    : state_(std::move(state)), thread_(std::move(thread)) {}

}  // namespace zebes
