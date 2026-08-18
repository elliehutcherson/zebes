#pragma once

#include <memory>
#include <thread>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace zebes {

// A joinable thread that runs one blocking callback.
//
// Wait returns the callback's status. Thread startup failures and exceptions
// escaping the callback are translated to Status at this standard-library
// boundary. Destruction joins, so the owner must arrange for a long-lived
// callback to terminate before destroying it.
class BlockingCallbackThread {
 public:
  using Callback = absl::AnyInvocable<absl::Status()>;

  ~BlockingCallbackThread();

  BlockingCallbackThread(BlockingCallbackThread&& other) noexcept = default;
  BlockingCallbackThread& operator=(BlockingCallbackThread&& other) = delete;

  BlockingCallbackThread(const BlockingCallbackThread&) = delete;
  BlockingCallbackThread& operator=(const BlockingCallbackThread&) = delete;

  static absl::StatusOr<BlockingCallbackThread> Start(Callback callback);

  // Waits for termination and returns the callback status or a translated
  // exception. Repeated waits return the same result. Concurrent calls to Wait
  // are not supported.
  absl::Status Wait();

 private:
  struct State {
    absl::Status result = absl::UnknownError("Blocking thread has not completed");
  };

  BlockingCallbackThread(std::unique_ptr<State> state, std::thread thread);

  // The thread is destroyed first so the callback cannot outlive its state.
  std::unique_ptr<State> state_;
  std::thread thread_;
};

}  // namespace zebes
