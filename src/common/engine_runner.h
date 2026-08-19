#pragma once

#include <atomic>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/engine.h"
#include "common/notification.h"
#include "common/notification_set.h"

namespace zebes {

// Repeatedly polls an Engine and blocks only after the engine reports idle.
// The engine owns the notification set and every wake source in it; the runner
// borrows both and adds its own stop source. Stop is thread-safe and wakes a
// blocked Run. A runner is single-use, and an engine accepts only one runner.
class EngineRunner {
 public:
  ~EngineRunner();

  EngineRunner(const EngineRunner&) = delete;
  EngineRunner& operator=(const EngineRunner&) = delete;

  // Seals the engine's notification set, so a second runner for the same
  // engine fails.
  static absl::StatusOr<std::unique_ptr<EngineRunner>> Create(Engine& engine);

  absl::Status Run();
  void Stop() noexcept;

 private:
  enum class State {
    kReady,
    kRunning,
    kStopping,
    kStopped,
  };

  EngineRunner(Engine& engine, NotificationSet& notification_set, Notification& stop_notification);

  absl::Status RunLoop();

  Engine& engine_;
  NotificationSet& notification_set_;
  Notification& stop_notification_;
  std::atomic<State> state_ = State::kReady;
};

}  // namespace zebes
