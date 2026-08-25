#include "editor/parallax_theme_editor/parallax_theme_editor_model.h"

#include <algorithm>
#include <limits>
#include <set>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"

namespace zebes {
namespace {

absl::StatusOr<int> NextElementId(const ParallaxLayer& layer) {
  std::set<int> ids;
  for (const ParallaxElement& element : layer.elements) {
    if (element.id < 0 || !ids.insert(element.id).second) {
      return absl::FailedPreconditionError(
          "Existing parallax element IDs must be unique and nonnegative.");
    }
  }
  for (int candidate = 0;; ++candidate) {
    if (!ids.contains(candidate)) return candidate;
    if (candidate == std::numeric_limits<int>::max()) {
      return absl::ResourceExhaustedError("No parallax element IDs remain available.");
    }
  }
}

}  // namespace

void ParallaxThemeEditorModel::BeginNew() {
  draft_ = ParallaxTheme{.name = "New Theme"};
  snapshot_.reset();
  selected_layer_.reset();
  selected_element_id_.reset();
  AddLayer().IgnoreError();
}

void ParallaxThemeEditorModel::Open(const ParallaxTheme& theme) {
  draft_ = theme;
  snapshot_ = theme;
  selected_layer_ = theme.layers.empty() ? std::nullopt : std::optional<int>(0);
  ReconcileSelection();
}

void ParallaxThemeEditorModel::Close() {
  draft_.reset();
  snapshot_.reset();
  selected_layer_.reset();
  selected_element_id_.reset();
}

absl::Status ParallaxThemeEditorModel::DiscardChanges() {
  if (!draft_) return absl::FailedPreconditionError("No parallax theme draft is open.");
  if (!snapshot_) {
    if (!draft_->id.empty()) {
      return absl::FailedPreconditionError("Saved parallax theme snapshot is unavailable.");
    }
    BeginNew();
    return absl::OkStatus();
  }
  draft_ = *snapshot_;
  ReconcileSelection();
  return absl::OkStatus();
}

void ParallaxThemeEditorModel::SelectLayer(int index) {
  selected_layer_ = index;
  selected_element_id_.reset();
  ReconcileSelection();
}

void ParallaxThemeEditorModel::SelectElement(int element_id) {
  selected_element_id_ = element_id;
  ReconcileSelection();
}

absl::Status ParallaxThemeEditorModel::AddLayer() {
  if (!draft_) return absl::FailedPreconditionError("No parallax theme draft is open.");
  draft_->layers.push_back({
      .name = absl::StrCat("Layer ", draft_->layers.size() + 1),
      .scroll_factor = {0.20, 0.10},
      .elements = {{.id = 0, .name = "Element 1"}},
  });
  selected_layer_ = static_cast<int>(draft_->layers.size()) - 1;
  selected_element_id_ = 0;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::DeleteSelectedLayer() {
  if (!draft_ || !selected_layer_) {
    return absl::FailedPreconditionError("No parallax layer is selected.");
  }
  draft_->layers.erase(draft_->layers.begin() + *selected_layer_);
  selected_element_id_.reset();
  ReconcileSelection();
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::AddElement() { return AddElementAt({0, 0}); }

absl::Status ParallaxThemeEditorModel::AddElementAt(Vec position) {
  if (!draft_ || !selected_layer_) {
    return absl::FailedPreconditionError("No parallax layer is selected.");
  }
  ParallaxLayer& layer = draft_->layers[*selected_layer_];
  ASSIGN_OR_RETURN(const int next_id, NextElementId(layer));
  layer.elements.push_back({
      .id = next_id,
      .name = absl::StrCat("Element ", layer.elements.size() + 1),
      .position = position,
  });
  selected_element_id_ = next_id;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::DuplicateSelectedElement() {
  if (!draft_ || !selected_layer_ || !selected_element_id_) {
    return absl::FailedPreconditionError("No parallax element is selected.");
  }
  ParallaxLayer& layer = draft_->layers[*selected_layer_];
  const auto found = std::find_if(
      layer.elements.begin(), layer.elements.end(),
      [this](const ParallaxElement& element) { return element.id == *selected_element_id_; });
  if (found == layer.elements.end()) {
    return absl::FailedPreconditionError("Selected parallax element no longer exists.");
  }
  ASSIGN_OR_RETURN(const int next_id, NextElementId(layer));
  ParallaxElement duplicate = *found;
  duplicate.id = next_id;
  duplicate.name = absl::StrCat(duplicate.name, " Copy");
  layer.elements.insert(found + 1, std::move(duplicate));
  selected_element_id_ = next_id;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::DeleteSelectedElement() {
  if (!draft_ || !selected_layer_ || !selected_element_id_) {
    return absl::FailedPreconditionError("No parallax element is selected.");
  }
  ParallaxLayer& layer = draft_->layers[*selected_layer_];
  const auto found = std::find_if(
      layer.elements.begin(), layer.elements.end(),
      [this](const ParallaxElement& element) { return element.id == *selected_element_id_; });
  if (found == layer.elements.end()) {
    return absl::FailedPreconditionError("Selected parallax element no longer exists.");
  }
  layer.elements.erase(found);
  selected_element_id_.reset();
  ReconcileSelection();
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::MoveSelectedElement(int delta) {
  if (!draft_ || !selected_layer_ || !selected_element_id_) {
    return absl::FailedPreconditionError("No parallax element is selected.");
  }
  ParallaxLayer& layer = draft_->layers[*selected_layer_];
  const auto found = std::find_if(
      layer.elements.begin(), layer.elements.end(),
      [this](const ParallaxElement& element) { return element.id == *selected_element_id_; });
  if (found == layer.elements.end()) {
    return absl::FailedPreconditionError("Selected parallax element no longer exists.");
  }
  const int index = static_cast<int>(found - layer.elements.begin());
  const int destination = index + delta;
  if (destination < 0 || destination >= static_cast<int>(layer.elements.size())) {
    return absl::OutOfRangeError("Parallax element cannot move beyond the layer bounds.");
  }
  std::swap(layer.elements[index], layer.elements[destination]);
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::MoveSelectedLayer(int delta) {
  if (!draft_ || !selected_layer_) {
    return absl::FailedPreconditionError("No parallax layer is selected.");
  }
  const int destination = *selected_layer_ + delta;
  if (destination < 0 || destination >= static_cast<int>(draft_->layers.size())) {
    return absl::OutOfRangeError("Parallax layer cannot move beyond the theme bounds.");
  }
  std::swap(draft_->layers[*selected_layer_], draft_->layers[destination]);
  selected_layer_ = destination;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::ApplyDepthPreset(ParallaxDepthPreset preset) {
  if (!draft_ || !selected_layer_) {
    return absl::FailedPreconditionError("No parallax layer is selected.");
  }

  Vec scroll_factor;
  switch (preset) {
    case ParallaxDepthPreset::kFar:
      scroll_factor = {0.05, 0.05};
      break;
    case ParallaxDepthPreset::kMiddle:
      scroll_factor = {0.20, 0.10};
      break;
    case ParallaxDepthPreset::kNearBackground:
      scroll_factor = {0.50, 0.25};
      break;
  }
  draft_->layers[*selected_layer_].scroll_factor = scroll_factor;
  return absl::OkStatus();
}

absl::StatusOr<ParallaxTheme> ParallaxThemeEditorModel::BuildSaveRequest() const {
  if (!draft_) return absl::FailedPreconditionError("No parallax theme draft is open.");
  ParallaxTheme request = *draft_;
  if (request.id.empty()) request.id = "pending";
  RETURN_IF_ERROR(ValidateParallaxTheme(request));
  request.id = draft_->id;
  return request;
}

void ParallaxThemeEditorModel::FinishSave(const std::string& id) {
  if (!draft_) return;
  draft_->id = id;
  snapshot_ = *draft_;
}

void ParallaxThemeEditorModel::ReconcileSelection() {
  if (!draft_ || draft_->layers.empty()) {
    selected_layer_.reset();
    selected_element_id_.reset();
    return;
  }
  if (!selected_layer_) selected_layer_ = 0;
  *selected_layer_ = std::clamp(*selected_layer_, 0, static_cast<int>(draft_->layers.size()) - 1);
  const ParallaxLayer& layer = draft_->layers[*selected_layer_];
  if (layer.elements.empty()) {
    selected_element_id_.reset();
    return;
  }
  const bool selection_exists =
      selected_element_id_.has_value() &&
      std::any_of(layer.elements.begin(), layer.elements.end(),
                  [this](const ParallaxElement& item) { return item.id == *selected_element_id_; });
  if (!selection_exists) selected_element_id_ = layer.elements.front().id;
}

}  // namespace zebes
