#pragma once

#include "absl/strings/string_view.h"
#include "engine/input_types.h"

namespace zebes {

// Maps named actions onto keys, so callers ask about intent rather than
// hardware. An action may be bound to several keys and is active while any of
// them is down.
//
// Update() samples the input source once and holds it for the rest of the
// frame, so every query in between agrees. IsActionJustPressed compares that
// sample against the previous one, which makes exactly one Update() per frame
// part of the contract: call it twice and the edge is consumed, so the press is
// never reported.
//
// Querying an unbound action reports inactive rather than failing -- a missing
// binding just means no key is assigned. QuitRequested latches, so it cannot be
// missed by polling on the wrong frame.
class IInputManager {
 public:
  virtual ~IInputManager() = default;

  virtual void BindAction(absl::string_view action_name, Key key) = 0;

  virtual void Update() = 0;

  virtual bool IsActionActive(absl::string_view action_name) const = 0;

  virtual bool IsActionJustPressed(absl::string_view action_name) const = 0;

  // Returns the platform-neutral state captured by the most recent Update.
  // Simulation code owns edge detection across fixed ticks, so it consumes
  // this value instead of render-frame action edges.
  virtual InputSnapshot CurrentSnapshot() const = 0;

  virtual bool QuitRequested() const = 0;
};

}  // namespace zebes
