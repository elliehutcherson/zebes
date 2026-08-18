#pragma once

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/notification.h"

namespace zebes {

enum class RunFeedback {
  kDidWork,
  kIdle,
};

// One non-blocking polling pass for a long-lived worker.
//
// Implementations inspect their input sources, perform a bounded amount of
// work, and report whether they made progress. They must not wait for new work;
// EngineRunner owns blocking and wakeup behavior.
class Engine {
 public:
  virtual ~Engine() = default;

  // Returns stable, non-null notification pointers that remain valid for the
  // lifetime of the EngineRunner created for this engine.
  virtual absl::Span<Notification* const> Notifications() const = 0;
  virtual absl::StatusOr<RunFeedback> Run() = 0;
};

}  // namespace zebes
