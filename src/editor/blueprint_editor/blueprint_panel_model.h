#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/asset_catalog.h"
#include "objects/blueprint.h"

namespace zebes {

// Owns BlueprintPanel's catalog, selection, and editable blueprint state
// without depending on ImGui, SDL, or the application API.
class BlueprintPanelModel {
 public:
  using BlueprintCatalog = std::map<AssetCatalogKey, Blueprint>;

  void SetBlueprints(std::vector<Blueprint> blueprints);
  const BlueprintCatalog& blueprints() const { return blueprints_; }

  absl::Status SelectBlueprint(const std::string& id);
  void ClearBlueprintSelection();
  bool has_blueprint_selection() const { return !selected_blueprint_id_.empty(); }
  const std::string& selected_blueprint_id() const { return selected_blueprint_id_; }

  void BeginNewBlueprint();
  absl::Status BeginEditingSelectedBlueprint();
  void CloseActiveBlueprint();
  bool has_active_blueprint() const { return active_blueprint_.has_value(); }
  bool is_new_blueprint() const;
  Blueprint* active_blueprint();
  const Blueprint* active_blueprint() const;

  // Whether the blueprint being edited differs from the state it was opened or
  // last saved at. Comparing against a snapshot rather than latching a flag
  // means no mutating path can forget to mark itself, and undoing an edit by
  // hand correctly reports clean again.
  bool has_unsaved_changes() const;

  absl::StatusOr<Blueprint> BuildSaveRequest() const;
  absl::Status FinishCreate(const std::string& saved_id);
  // Makes the current state the clean one, after a successful write.
  void MarkSaved();
  void FinishDelete();

  absl::Status AddState();
  absl::Status DeleteState(int state_index);
  absl::Status ValidateStateIndex(int state_index) const;

 private:
  const Blueprint* FindBlueprint(const std::string& id) const;

  BlueprintCatalog blueprints_;
  std::string selected_blueprint_id_;
  std::optional<Blueprint> active_blueprint_;
  // The active blueprint as it stood when editing began or was last saved.
  std::optional<Blueprint> baseline_blueprint_;
};

}  // namespace zebes
