#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/asset_catalog.h"
#include "objects/collider.h"

namespace zebes {

// Owns ColliderPanel's selection and editable collider state without depending
// on ImGui, SDL, or the application API.
class ColliderPanelModel {
 public:
  using ColliderCatalog = std::map<AssetCatalogKey, Collider>;

  void SetColliders(std::vector<Collider> colliders);
  const ColliderCatalog& colliders() const { return colliders_; }

  absl::Status SelectCollider(const std::string& id);
  void ClearColliderSelection();
  bool has_collider_selection() const { return !selected_collider_id_.empty(); }
  const std::string& selected_collider_id() const { return selected_collider_id_; }

  void BeginNewCollider();
  absl::Status BeginEditingSelectedCollider();
  absl::Status BeginEditingCollider(const std::string& id);
  void CloseActiveCollider();
  bool has_active_collider() const { return active_collider_.has_value(); }
  bool is_new_collider() const;
  Collider* active_collider();
  const Collider* active_collider() const;

  absl::StatusOr<Collider> BuildSaveRequest() const;
  absl::Status FinishCreate(const std::string& saved_id);
  void FinishDelete();
  absl::Status ResetActiveCollider();

  absl::Status AddPolygon();
  absl::Status DeletePolygon(std::size_t polygon_index);
  absl::Status AddVertex(std::size_t polygon_index);
  absl::Status DeleteVertex(std::size_t polygon_index, std::size_t vertex_index);

  // Changes whenever the active Collider object or its polygon structure is
  // replaced. CanvasCollider uses this to rebuild its reference and indexed
  // interaction state safely.
  std::uint64_t active_revision() const { return active_revision_; }

 private:
  const Collider* FindCollider(const std::string& id) const;
  void ReplaceActiveCollider(Collider collider);

  ColliderCatalog colliders_;
  std::string selected_collider_id_;
  std::optional<Collider> active_collider_;
  std::uint64_t active_revision_ = 0;
};

}  // namespace zebes
