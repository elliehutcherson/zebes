#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/api.h"
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

  // Canvas state
  int canvas_width_ = 800;
  int canvas_height_ = 600;
  float canvas_zoom_ = 1.0f;
  Vec canvas_offset_ = {0, 0};
  bool is_dragging_ = false;

  void RenderControls();
  void RenderCreator();
  void RenderCreateState();
  void RenderEditor();

  // Sub-renderers to reduce complexity
  void RenderCanvas(ImVec2 canvas_sz, ImVec2 canvas_p0);
  void RenderColliderPanel();

  // Helper functions
  void CreateNewCollider();
  void SaveCurrentCollider();
  void LoadCollider(const std::string& id);
};

}  // namespace zebes
