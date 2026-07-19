#include "editor/blueprint_editor/collider_panel_model.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {

void ColliderPanelModel::SetColliders(std::vector<Collider> colliders) {
  colliders_.clear();
  for (Collider& collider : colliders) {
    AssetCatalogKey key{.display_name = collider.name, .id = collider.id};
    colliders_.emplace(std::move(key), std::move(collider));
  }
  if (FindCollider(selected_collider_id_) == nullptr) ClearColliderSelection();
}

absl::Status ColliderPanelModel::SelectCollider(const std::string& id) {
  if (FindCollider(id) == nullptr) return absl::NotFoundError("Collider was not found");
  selected_collider_id_ = id;
  return absl::OkStatus();
}

void ColliderPanelModel::ClearColliderSelection() { selected_collider_id_.clear(); }

void ColliderPanelModel::BeginNewCollider() {
  ReplaceActiveCollider(
      Collider{.name = absl::StrCat("collider_", colliders_.size())});
}

absl::Status ColliderPanelModel::BeginEditingSelectedCollider() {
  if (!has_collider_selection()) {
    return absl::FailedPreconditionError("No collider is selected");
  }
  return BeginEditingCollider(selected_collider_id_);
}

absl::Status ColliderPanelModel::BeginEditingCollider(const std::string& id) {
  const Collider* collider = FindCollider(id);
  if (collider == nullptr) return absl::NotFoundError("Collider was not found");
  selected_collider_id_ = id;
  ReplaceActiveCollider(*collider);
  return absl::OkStatus();
}

void ColliderPanelModel::CloseActiveCollider() {
  active_collider_.reset();
  ++active_revision_;
}

bool ColliderPanelModel::is_new_collider() const {
  return active_collider_.has_value() && active_collider_->id.empty();
}

Collider* ColliderPanelModel::active_collider() {
  return active_collider_.has_value() ? &*active_collider_ : nullptr;
}

const Collider* ColliderPanelModel::active_collider() const {
  return active_collider_.has_value() ? &*active_collider_ : nullptr;
}

absl::StatusOr<Collider> ColliderPanelModel::BuildSaveRequest() const {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  return *active_collider_;
}

absl::Status ColliderPanelModel::FinishCreate(const std::string& saved_id) {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  if (!active_collider_->id.empty()) {
    return absl::FailedPreconditionError("Active collider has already been created");
  }
  if (saved_id.empty()) return absl::InvalidArgumentError("Saved collider ID cannot be empty");
  active_collider_->id = saved_id;
  selected_collider_id_ = saved_id;
  return absl::OkStatus();
}

void ColliderPanelModel::FinishDelete() {
  CloseActiveCollider();
  ClearColliderSelection();
}

absl::Status ColliderPanelModel::ResetActiveCollider() {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  if (active_collider_->id.empty()) {
    BeginNewCollider();
    return absl::OkStatus();
  }

  const Collider* persisted = FindCollider(active_collider_->id);
  if (persisted == nullptr) return absl::NotFoundError("Collider was not found");
  ReplaceActiveCollider(*persisted);
  return absl::OkStatus();
}

absl::Status ColliderPanelModel::AddPolygon() {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  active_collider_->polygons.push_back({{0, 0}, {50, 0}, {50, 50}, {0, 50}});
  ++active_revision_;
  return absl::OkStatus();
}

absl::Status ColliderPanelModel::DeletePolygon(std::size_t polygon_index) {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  if (polygon_index >= active_collider_->polygons.size()) {
    return absl::OutOfRangeError("Polygon index is out of range");
  }
  active_collider_->polygons.erase(active_collider_->polygons.begin() + polygon_index);
  ++active_revision_;
  return absl::OkStatus();
}

absl::Status ColliderPanelModel::AddVertex(std::size_t polygon_index) {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  if (polygon_index >= active_collider_->polygons.size()) {
    return absl::OutOfRangeError("Polygon index is out of range");
  }
  active_collider_->polygons[polygon_index].push_back({0, 0});
  ++active_revision_;
  return absl::OkStatus();
}

absl::Status ColliderPanelModel::DeleteVertex(std::size_t polygon_index,
                                               std::size_t vertex_index) {
  if (!active_collider_.has_value()) {
    return absl::FailedPreconditionError("No collider is being edited");
  }
  if (polygon_index >= active_collider_->polygons.size()) {
    return absl::OutOfRangeError("Polygon index is out of range");
  }
  Polygon& polygon = active_collider_->polygons[polygon_index];
  if (vertex_index >= polygon.size()) {
    return absl::OutOfRangeError("Vertex index is out of range");
  }
  polygon.erase(polygon.begin() + vertex_index);
  ++active_revision_;
  return absl::OkStatus();
}

const Collider* ColliderPanelModel::FindCollider(const std::string& id) const {
  for (const auto& catalog_entry : colliders_) {
    if (catalog_entry.second.id == id) return &catalog_entry.second;
  }
  return nullptr;
}

void ColliderPanelModel::ReplaceActiveCollider(Collider collider) {
  active_collider_ = std::move(collider);
  ++active_revision_;
}

}  // namespace zebes
