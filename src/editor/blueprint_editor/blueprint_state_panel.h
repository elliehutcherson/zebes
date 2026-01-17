#pragma once

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "objects/blueprint.h"

namespace zebes {

class BlueprintStatePanel {
 public:
  // Creates a new BlueprintStatePanel instance.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<BlueprintStatePanel>> Create(Api* api, GuiInterface* gui);

  // Renders the blueprint state panel UI.
  void Render();

  // Sets the state to be edited.
  void SetState(Blueprint& blueprint, int index);

  int GetStateIndex() { return index_; }

  // Resets the panel state.
  void Reset();

 private:
  BlueprintStatePanel(Api* api, GuiInterface* gui);

  Api* api_;
  GuiInterface* gui_;
  Blueprint* blueprint_ = nullptr;
  int index_ = -1;
  std::string current_name_;
};

}  // namespace zebes