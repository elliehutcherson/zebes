#include "editor/level_editor/world_layer_model.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "objects/level.h"

namespace zebes {
namespace {

template <typename Set>
void RemoveMissingLayerIds(Set& ids, const Level& level) {
  for (auto it = ids.begin(); it != ids.end();) {
    if (FindWorldLayer(level, *it) != nullptr) {
      ++it;
      continue;
    }
    const int missing_id = *it;
    ++it;
    ids.erase(missing_id);
  }
}

auto FindLayerIterator(Level& level, int layer_id) {
  return std::find_if(level.layers.begin(), level.layers.end(),
                      [layer_id](const WorldLayer& layer) { return layer.id == layer_id; });
}

auto FindLayerIterator(const Level& level, int layer_id) {
  return std::find_if(level.layers.begin(), level.layers.end(),
                      [layer_id](const WorldLayer& layer) { return layer.id == layer_id; });
}

}  // namespace

void WorldLayerModel::Open(const Level& level) {
  hidden_layer_ids_.clear();
  locked_layer_ids_.clear();
  active_layer_id_ = level.layers.empty() ? -1 : level.layers.front().id;
}

void WorldLayerModel::Close() {
  active_layer_id_ = -1;
  hidden_layer_ids_.clear();
  locked_layer_ids_.clear();
}

void WorldLayerModel::Reconcile(const Level& level) {
  RemoveMissingLayerIds(hidden_layer_ids_, level);
  RemoveMissingLayerIds(locked_layer_ids_, level);
  if (FindWorldLayer(level, active_layer_id_) != nullptr) return;
  active_layer_id_ = level.layers.empty() ? -1 : level.layers.front().id;
}

WorldLayer* WorldLayerModel::active_layer(Level& level) const {
  return FindWorldLayer(level, active_layer_id_);
}

const WorldLayer* WorldLayerModel::active_layer(const Level& level) const {
  return FindWorldLayer(level, active_layer_id_);
}

absl::Status WorldLayerModel::Activate(const Level& level, int layer_id) {
  if (FindWorldLayer(level, layer_id) == nullptr) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }
  active_layer_id_ = layer_id;
  return absl::OkStatus();
}

absl::StatusOr<int> WorldLayerModel::AddLayer(Level& level) {
  Reconcile(level);
  if (level.layers.empty()) {
    return absl::FailedPreconditionError("Cannot add relative to a layerless level.");
  }
  ASSIGN_OR_RETURN(const int id, NextAvailableWorldLayerId(level));
  auto active = FindLayerIterator(level, active_layer_id_);
  if (active == level.layers.end()) {
    return absl::FailedPreconditionError("Active world layer is unavailable.");
  }
  const auto insertion = std::next(active);
  level.layers.insert(insertion, WorldLayer{.id = id, .name = absl::StrCat("Layer ", id)});
  active_layer_id_ = id;
  return id;
}

bool WorldLayerModel::CanMoveForward(const Level& level, int layer_id) {
  const auto found = FindLayerIterator(level, layer_id);
  return found != level.layers.end() && std::next(found) != level.layers.end();
}

bool WorldLayerModel::CanMoveBackward(const Level& level, int layer_id) {
  const auto found = FindLayerIterator(level, layer_id);
  return found != level.layers.end() && found != level.layers.begin();
}

absl::Status WorldLayerModel::MoveForward(Level& level, int layer_id) {
  auto found = FindLayerIterator(level, layer_id);
  if (found == level.layers.end()) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }
  auto next = std::next(found);
  if (next == level.layers.end()) {
    return absl::FailedPreconditionError("World layer is already frontmost.");
  }
  std::iter_swap(found, next);
  return absl::OkStatus();
}

absl::Status WorldLayerModel::MoveBackward(Level& level, int layer_id) {
  auto found = FindLayerIterator(level, layer_id);
  if (found == level.layers.end()) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }
  if (found == level.layers.begin()) {
    return absl::FailedPreconditionError("World layer is already backmost.");
  }
  std::iter_swap(std::prev(found), found);
  return absl::OkStatus();
}

absl::Status WorldLayerModel::DeleteLayer(Level& level, int layer_id) {
  if (level.layers.size() <= 1) {
    return absl::FailedPreconditionError("A level must retain at least one world layer.");
  }
  auto found = FindLayerIterator(level, layer_id);
  if (found == level.layers.end()) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }

  const size_t index = static_cast<size_t>(std::distance(level.layers.begin(), found));
  level.layers.erase(found);
  hidden_layer_ids_.erase(layer_id);
  locked_layer_ids_.erase(layer_id);
  if (active_layer_id_ == layer_id) {
    const size_t replacement = std::min(index, level.layers.size() - 1);
    active_layer_id_ = level.layers[replacement].id;
  }
  return absl::OkStatus();
}

absl::Status WorldLayerModel::SetVisible(const Level& level, int layer_id, bool visible) {
  if (FindWorldLayer(level, layer_id) == nullptr) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }
  if (visible) {
    hidden_layer_ids_.erase(layer_id);
  } else {
    hidden_layer_ids_.insert(layer_id);
  }
  return absl::OkStatus();
}

absl::Status WorldLayerModel::SetLocked(const Level& level, int layer_id, bool locked) {
  if (FindWorldLayer(level, layer_id) == nullptr) {
    return absl::NotFoundError(absl::StrCat("World layer ", layer_id, " was not found."));
  }
  if (locked) {
    locked_layer_ids_.insert(layer_id);
  } else {
    locked_layer_ids_.erase(layer_id);
  }
  return absl::OkStatus();
}

}  // namespace zebes
