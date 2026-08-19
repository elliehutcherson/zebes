#pragma once

#include <optional>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "common/notification_set.h"

namespace zebes {

// Whether the runner may sleep after this pass. kIdle lets it sleep; kDidWork
// sends it straight into another pass.
enum class RunFeedback {
  kDidWork,
  kIdle,
};

// What one polling pass reports back to its runner.
struct RunResult {
  RunFeedback feedback = RunFeedback::kIdle;

  // How long the runner may sleep, and meaningful only with kIdle. Absent means
  // sleep until a notification fires. A time means sleep until a notification
  // fires or that time passes, whichever comes first; one already in the past
  // sends the runner straight into the next pass.
  //
  // A deadline is how an engine stays honest about a source it can only
  // discover by polling: a remote transfer with no registered descriptor, or a
  // fixed timestep that is due whether or not anything notifies. Both are idle
  // right now and neither may sleep indefinitely.
  std::optional<absl::Time> wake_deadline;
};

// One non-blocking polling pass for a long-lived worker.
//
// Implementations inspect their input sources, perform a bounded amount of
// work, and report whether they made progress. They must not wait for new work;
// EngineRunner owns blocking and wakeup behavior.
class Engine {
 public:
  virtual ~Engine() = default;

  // The engine's wake sources, created during engine construction and owning
  // every Notification the engine's producers signal. It must outlive the
  // EngineRunner created for this engine. Implementations return the same set
  // on every call.
  virtual NotificationSet& notification_set() = 0;

  // Report kIdle with no deadline only when every source that can produce work
  // has a notification in the set, because the runner will then sleep until one
  // fires. A source the engine can only poll has no notification, so a pass
  // that found nothing there is kIdle with the deadline by which the engine
  // wants to look again.
  virtual absl::StatusOr<RunResult> Run() = 0;
};

}  // namespace zebes
