#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "objects/blueprint.h"

namespace zebes {

class BlueprintPanel {
 public:
  // Creates a new ColliderPanel instance.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<BlueprintPanel>> Create(Api* api);

  // Renders the collider panel UI.
  // This is the main entry point for the panel's rendering logic.
  // If -1 is returned, no blueprint state is being editted.
  // If a value greater than -1 is returned, some blueprint state is being editted.
  int Render();

  // Clears any internal state if necessary.
  void Clear();

  // Returns the currently selected collider, or nullptr if none.
  Blueprint* GetBlueprint();

 private:
  enum BlueprintPanelMode {
    kBlueprintPanelList,  // Display list of blueprints
    kBlueprintPanelNew,   // Create a new blueprint
    kBlueprintPanelEdit,  // Edit an existing blueprint
  };

  enum Op { kBlueprintCreate, kBlueprintUpdate, kBlueprintDelete };

  BlueprintPanel(Api* api);

  // Refreshes the local cache of colliders from the API.
  void RefreshBlueprintCache();

  // Renders the list of colliders and CRUD buttons.
  void RenderList();

  // Renders the details view for creating or editing a collider.
  int RenderDetails();

  int RenderStateList();

  bool RenderStateDetails(const std::string& name, int index, int* selected_index);

  void ConfirmState(Op op);

  BlueprintPanelMode mode_ = kBlueprintPanelList;
  std::vector<Blueprint> blueprint_cache_;

  int blueprint_index_ = -1;
  std::optional<Blueprint> editting_blueprint_;
  int state_index_ = -1;

  // Outside dependencies
  Api* api_ = nullptr;
};

}  // namespace zebes