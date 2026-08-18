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
// The Engine supplies its independent notification sources. Stop is thread-safe
// and wakes a blocked Run. A runner is single-use.
class EngineRunner {
 public:
  ~EngineRunner();

  EngineRunner(const EngineRunner&) = delete;
  EngineRunner& operator=(const EngineRunner&) = delete;

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

  EngineRunner(Engine& engine, std::unique_ptr<Notification> stop_notification,
               NotificationSet notification_set);

  absl::Status RunLoop();

  Engine& engine_;
  std::unique_ptr<Notification> stop_notification_;
  NotificationSet notification_set_;
  std::atomic<State> state_ = State::kReady;
};

}  // namespace zebes
