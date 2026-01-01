#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "objects/collider.h"

namespace zebes {

struct ColliderResult {
  enum class Type : uint8_t { kNone, kAttach, kDetach };
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
  ColliderResult Render();

  // Clears any internal state if necessary.
  void Clear();

  // Returns the currently selected collider, or nullptr if none.
  Collider* GetCollider() {
    if (!editting_collider_.has_value()) {
      return nullptr;
    }
    return &*editting_collider_;
  }

  void SetCollider(const std::string& id);

 private:
  enum Mode {
    kColliderPanelList,  // Display list of colliders
    kColliderPanelNew,   // Create a new collider
    kColliderPanelEdit,  // Edit an existing collider
  };

  enum Op { kColliderCreate, kColliderUpdate, kColliderDelete, kColliderReset };

  ColliderPanel(Api* api);

  // Refreshes the local cache of colliders from the API.
  void RefreshColliderCache();

  // Renders the list of colliders and CRUD buttons.
  ColliderResult RenderList();

  // Renders the details view for creating or editing a collider.
  ColliderResult RenderDetails();

  // Renders the list of polygons for the current collider.
  void RenderPolygonList();

  // Renders a single polygon's details (vertices, deletion).
  // Returns true if polygon was deleted.
  bool RenderPolygonDetails(Polygon& poly, int index);

  void ConfirmState(Op op);

  Mode mode_ = kColliderPanelList;
  std::vector<Collider> collider_cache_;

  int collider_index_ = 0;
  std::optional<std::string> attached_id_;
  std::optional<Collider> editting_collider_;

  // Outside dependencies
  Api* api_;
};

}  // namespace zebes