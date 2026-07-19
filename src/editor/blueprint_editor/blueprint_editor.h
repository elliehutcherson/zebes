#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/animator.h"
#include "editor/blueprint_editor/blueprint_panel.h"
#include "editor/blueprint_editor/blueprint_panel_model.h"
#include "editor/blueprint_editor/blueprint_state_panel.h"
#include "editor/blueprint_editor/collider_panel.h"
#include "editor/blueprint_editor/collider_panel_model.h"
#include "editor/blueprint_editor/sprite_panel.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "objects/camera.h"

namespace zebes {

struct ColliderResult {
  enum class Type : std::uint8_t { kNone, kAttach, kDetach };
  Type type = Type::kNone;
  std::string collider_id;
};

class BlueprintEditor {
 public:
  static absl::StatusOr<std::unique_ptr<BlueprintEditor>> Create(Api* api, GuiInterface* gui);

  ~BlueprintEditor() = default;

  absl::Status Render();

  // Saves the current blueprint state.
  // This is exposed for testing purposes and for the UI.
  void SaveBlueprint();

 private:
  // UI state
  enum class Mode {
    kBlueprint,
    kBlueprintState,
  };

  explicit BlueprintEditor(Api* api, GuiInterface* gui);
  absl::Status Init();

  absl::Status RenderLeftPanel();
  absl::Status RenderCanvas();
  absl::Status RenderRightPanel();

  // Mode specific rendering helpers
  absl::Status RenderBlueprintListMode();
  absl::Status RenderBlueprintStateMode();

  absl::Status EnterBlueprintStateMode(Blueprint& bp, int state_index);
  absl::Status ExitBlueprintStateMode();

  void RefreshBlueprintCatalog();
  absl::Status HandleBlueprintPanelEvent(const BlueprintPanel::Event& event);
  absl::Status SaveActiveBlueprint();
  void RefreshColliderCatalog();
  absl::StatusOr<ColliderResult> HandleColliderPanelAction(ColliderPanel::Action action);

  void UpdateStateCollider(const ColliderResult& collider_result);
  void UpdateStateSprite(const SpriteResult& sprite_result);

  Api* api_;
  GuiInterface* gui_;
  std::unique_ptr<Animator> animator_;
  Canvas canvas_;
  Camera camera_;

  std::unique_ptr<BlueprintPanel> blueprint_panel_;
  std::unique_ptr<BlueprintStatePanel> blueprint_state_panel_;
  std::unique_ptr<ColliderPanel> collider_panel_;
  std::unique_ptr<SpritePanel> sprite_panel_;
  BlueprintPanelModel blueprint_model_;
  ColliderPanelModel collider_model_;

  Mode mode_ = Mode::kBlueprint;
};

}  // namespace zebes
