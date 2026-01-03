#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas_collider.h"
#include "objects/collider.h"

namespace zebes {

struct ColliderResult {
  enum Type : uint8_t { kNone = 0, kAttach = 1, kDetach = 2 };
  Type type = Type::kNone;
  std::string collider_id;
};

class ColliderPanel {
 public:
  // Creates a new ColliderPanel instance.
  // Returns an error if the API pointer is null.
  // If attach is true, an "Attach" button will be rendered. If clicked, "Render" will return
  // "kAttach" or "kDetach" when the user attaches or detaches a valid collider.
  static absl::StatusOr<std::unique_ptr<ColliderPanel>> Create(Api* api);

  // Renders the collider panel UI.
  // This is the main entry point for the panel's rendering logic.
  absl::StatusOr<ColliderResult> Render();

  absl::StatusOr<bool> RenderCanvas(Canvas& canvas, bool input_allowed);

  absl::Status Attach(const std::string& id);

  void Detach();

 private:
  enum Op : uint8_t { kColliderCreate, kColliderUpdate, kColliderDelete, kColliderReset };

  ColliderPanel(Api* api);

  absl::Status Attach(int i);

  // Refreshes the local cache of colliders from the API.
  void RefreshColliderCache();

  // Renders the list of colliders and CRUD buttons.
  absl::StatusOr<ColliderResult> RenderList();

  // Renders the details view for creating or editing a collider.
  absl::StatusOr<ColliderResult> RenderDetails();

  // Renders the list of polygons for the current collider.
  void RenderPolygonList();

  // Renders a single polygon's details (vertices, deletion).
  // Returns true if polygon was deleted.
  bool RenderPolygonDetails(Polygon& poly, int index);

  absl::Status ConfirmState(Op op);

  int selected_index_ = -1;
  std::vector<Collider> collider_cache_;
  std::optional<Collider> editting_collider_;
  std::unique_ptr<CanvasCollider> canvas_collider_;

  // Outside dependencies
  Api& api_;
};

}  // namespace zebes