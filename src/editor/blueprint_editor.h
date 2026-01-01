#pragma once

#include <memory>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/animator.h"
#include "editor/blueprint_panel.h"
#include "editor/blueprint_state_panel.h"
#include "editor/canvas.h"
#include "editor/canvas_collider.h"
#include "editor/canvas_sprite.h"
#include "editor/collider_panel.h"
#include "editor/sprite_panel.h"

namespace zebes {

class BlueprintEditor {
 public:
  static absl::StatusOr<std::unique_ptr<BlueprintEditor>> Create(Api* api);

  ~BlueprintEditor() = default;

  void Render();

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

  void ExitBlueprintStateMode();

  void UpdateStateCollider(const ColliderResult& collider_result);
  void UpdateStateSprite(const SpriteResult& sprite_result);

  Api* api_;
  std::unique_ptr<Animator> animator_;
  Canvas canvas_;
  std::optional<CanvasSprite> canvas_sprite_;
  std::optional<CanvasCollider> canvas_collider_;

  std::unique_ptr<BlueprintPanel> blueprint_panel_;
  std::unique_ptr<BlueprintStatePanel> blueprint_state_panel_;
  std::unique_ptr<ColliderPanel> collider_panel_;
  std::unique_ptr<SpritePanel> sprite_panel_;

  Mode mode_ = Mode::kBlueprint;
};

}  // namespace zebes
