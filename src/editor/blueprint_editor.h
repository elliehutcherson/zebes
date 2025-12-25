#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/animator.h"
#include "imgui.h"
#include "objects/blueprint.h"
#include "objects/collider.h"
#include "objects/sprite.h"

namespace zebes {

class BlueprintEditor {
 public:
  static absl::StatusOr<std::unique_ptr<BlueprintEditor>> Create(Api* api);

  ~BlueprintEditor() = default;

  void Render();

 private:
  explicit BlueprintEditor(Api* api);

  void RefreshBlueprintList();
  void SelectBlueprint(const std::string& blueprint_id);
  void CreateBlueprint();
  void UpdateBlueprint();
  void DeleteBlueprint();

  Api* api_;
  std::unique_ptr<Animator> animator_;

  // UI state
  enum class Mode {
    kList,
    kCreate,
    kCreateState,
  };
  Mode mode_ = Mode::kList;
  std::string selected_blueprint_id_;
  std::vector<Blueprint> blueprint_cache_;
  std::string name_buffer_;  // For blueprint name

  // State creation UI state
  struct PendingState {
    std::string name;
    std::string sprite_id;
    std::string collider_id;
  };
  std::vector<PendingState> pending_states_;
  std::string state_name_buffer_;
  int selected_sprite_index_ = -1;

  // Blueprint state
  Blueprint selected_blueprint_;  // For editing existing
  Blueprint creation_blueprint_;  // For creating new

  // Collider editing state
  Collider current_collider_;
  std::string current_buffer_collider_id_;
  int selected_polygon_index_ = -1;
  int selected_vertex_index_ = -1;

  // Sprite editing state
  Sprite original_sprite_;
  Sprite editing_sprite_;
  int selected_frame_index_ = 0;
  std::vector<Sprite> sprite_cache_;
  bool is_playing_animation_ = false;
  double animation_timer_ = 0.0;

  // Canvas state
  float canvas_zoom_ = 1.0f;
  Vec canvas_offset_ = {0, 0};
  bool is_dragging_ = false;
  bool is_dragging_sprite_ = false;
  // Origin position for coordinate conversion, cached each frame
  ImVec2 origin_ = {0, 0};

  // Snapping state
  bool snap_to_grid_ = true;
  double drag_accumulator_x_ = 0;
  double drag_accumulator_y_ = 0;

  // Renders the list of blueprints and control buttons (Create, Edit, Delete).
  void RenderControls();

  // Renders the view for creating a new blueprint.
  void RenderCreator();

  // Renders the view for creating a new state within a blueprint.
  void RenderCreateState();

  // Renders the main editor area (placeholder or details).
  void RenderEditor();

  // Sub-renderers to reduce complexity
  void RenderCanvas(ImVec2 canvas_sz, ImVec2 canvas_p0);
  void RenderSpriteOnCanvas(ImDrawList* draw_list, bool is_active_input);
  void RenderPolygonsOnCanvas(ImDrawList* draw_list, bool is_active_input);
  void HandleCanvasInteraction(const ImVec2& canvas_sz, const ImVec2& canvas_p0, bool is_hovered,
                               bool is_active);

  // Renders the sprite selection dropdown.
  void RenderSpriteSelector();

  // Renders the list of polygons for the current collider.
  void RenderPolygonList();

  // Renders the panel for editing collider properties.
  void RenderColliderPanel();

  // Renders the details of the selected sprite.
  void RenderSpriteDetails();

  // Renders the rulers on the canvas.
  void RenderRulers(ImDrawList* draw_list, ImVec2 canvas_p0, ImVec2 canvas_sz, ImVec2 origin);

  // Updates the animation state if playing.
  void UpdateAnimation();

  // Applies drag movement to value with optional snapping, using accumulator.
  void ApplyDrag(double& val, double& accumulator, double delta, bool snap);

  // Helper functions
  // Checks if the editing sprite has unsaved changes compared to the original.
  bool IsSpriteDirty() const;

  // Refreshes the local cache of sprites from the API.
  void RefreshSpriteList();

  // Helper functions
  void CreateNewCollider();
  void SaveCurrentCollider();
  void LoadCollider(const std::string& id);

  // Converts a world coordinate to a screen coordinate based on current zoom and pan.
  ImVec2 WorldToScreen(const Vec& v) const;

  // Converts a screen coordinate to a world coordinate based on current zoom and pan.
  Vec ScreenToWorld(const ImVec2& p) const;
};

}  // namespace zebes
