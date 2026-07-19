#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "objects/blueprint.h"

namespace zebes {

class BlueprintStatePanel {
 public:
  // Creates a new BlueprintStatePanel instance.
  static absl::StatusOr<std::unique_ptr<BlueprintStatePanel>> Create(GuiInterface* gui);

  // Renders the blueprint state panel UI.
  void Render();

  // Sets the state to be edited.
  void SetState(Blueprint& blueprint, int index);

  int GetStateIndex() { return index_; }

  // Resets the panel state.
  void Reset();

 private:
  explicit BlueprintStatePanel(GuiInterface* gui);

  GuiInterface* gui_;
  Blueprint* blueprint_ = nullptr;
  int index_ = -1;
};

}  // namespace zebes
