#pragma once

#include "absl/strings/string_view.h"
#include "engine/input_types.h"

namespace zebes {

class IInputManager {
 public:
  virtual ~IInputManager() = default;

  // --- 1. Registration API ---
  virtual void BindAction(absl::string_view action_name, Key key) = 0;

  // --- 2. Interception Loop ---
  virtual void Update() = 0;

  // --- 3. Query API ---
  virtual bool IsActionActive(absl::string_view action_name) const = 0;

  virtual bool IsActionJustPressed(absl::string_view action_name) const = 0;

  virtual bool QuitRequested() const = 0;
};

}  // namespace zebes
