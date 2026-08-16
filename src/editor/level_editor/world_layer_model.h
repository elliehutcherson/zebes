#pragma once

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/level.h"

namespace zebes {

// Owns transient world-layer authoring state and platform-neutral mutations.
// Visibility and locking deliberately do not enter the serialized Level.
class WorldLayerModel {
 public:
  // Resets session state for a newly opened level and selects its backmost
  // layer. A malformed layerless level is left with no active layer so callers
  // fail explicitly rather than inventing one.
  void Open(const Level& level);
  void Close();

  // Drops state for removed layers and selects a valid layer when possible.
  void Reconcile(const Level& level);

  int active_layer_id() const { return active_layer_id_; }
  WorldLayer* active_layer(Level& level) const;
  const WorldLayer* active_layer(const Level& level) const;

  absl::Status Activate(const Level& level, int layer_id);
  absl::StatusOr<int> AddLayer(Level& level);
  static absl::Status MoveForward(Level& level, int layer_id);
  static absl::Status MoveBackward(Level& level, int layer_id);
  absl::Status DeleteLayer(Level& level, int layer_id);

  static bool CanMoveForward(const Level& level, int layer_id);
  static bool CanMoveBackward(const Level& level, int layer_id);

  bool IsVisible(int layer_id) const { return !hidden_layer_ids_.contains(layer_id); }
  bool IsLocked(int layer_id) const { return locked_layer_ids_.contains(layer_id); }
  const absl::flat_hash_set<int>& hidden_layer_ids() const { return hidden_layer_ids_; }
  absl::Status SetVisible(const Level& level, int layer_id, bool visible);
  absl::Status SetLocked(const Level& level, int layer_id, bool locked);

 private:
  int active_layer_id_ = -1;
  absl::flat_hash_set<int> hidden_layer_ids_;
  absl::flat_hash_set<int> locked_layer_ids_;
};

}  // namespace zebes
