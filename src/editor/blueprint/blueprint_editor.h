#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/animator.h"
#include "editor/blueprint/blueprint_panel.h"
#include "editor/blueprint/blueprint_state_panel.h"
#include "editor/blueprint/collider_panel.h"
#include "editor/blueprint/sprite_panel.h"
#include "editor/canvas.h"

namespace zebes {

class BlueprintEditorReproTest;

class BlueprintEditor {
 public:
  static absl::StatusOr<std::unique_ptr<BlueprintEditor>> Create(Api* api);

  ~BlueprintEditor() = default;

  void Render();

  // Saves the current blueprint state.
  // This is exposed for testing purposes and for the UI.
  void SaveBlueprint();

 private:
  // UI state
  enum class Mode {
    kBlueprint,
    kBlueprintState,
  };

  explicit BlueprintEditor(Api* api);
  absl::Status Init();

  void RenderLeftPanel();
  void RenderCanvas();
  void RenderRightPanel();

  // Mode specific rendering helpers
  void RenderBlueprintListMode();
  void RenderBlueprintStateMode();

  void EnterBlueprintStateMode(Blueprint& bp, int state_index);
  void ExitBlueprintStateMode();

  void UpdateStateCollider(const ColliderResult& collider_result);
  void UpdateStateSprite(const SpriteResult& sprite_result);

  Api* api_;
  std::unique_ptr<Animator> animator_;
  Canvas canvas_;

  std::unique_ptr<BlueprintPanel> blueprint_panel_;
  std::unique_ptr<BlueprintStatePanel> blueprint_state_panel_;
  std::unique_ptr<ColliderPanel> collider_panel_;
  std::unique_ptr<SpritePanel> sprite_panel_;

  Mode mode_ = Mode::kBlueprint;
};

}  // namespace zebes