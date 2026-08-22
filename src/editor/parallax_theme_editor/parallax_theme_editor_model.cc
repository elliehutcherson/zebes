#include "editor/parallax_theme_editor/parallax_theme_editor_model.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace zebes {

void ParallaxThemeEditorModel::BeginNew() {
  draft_ = ParallaxTheme{.name = "New Theme"};
  snapshot_.reset();
  selected_layer_.reset();
  AddLayer().IgnoreError();
}

void ParallaxThemeEditorModel::Open(const ParallaxTheme& theme) {
  draft_ = theme;
  snapshot_ = theme;
  selected_layer_ = theme.layers.empty() ? std::nullopt : std::optional<int>(0);
}

void ParallaxThemeEditorModel::Close() {
  draft_.reset();
  snapshot_.reset();
  selected_layer_.reset();
}

void ParallaxThemeEditorModel::SelectLayer(int index) {
  selected_layer_ = index;
  ReconcileSelection();
}

absl::Status ParallaxThemeEditorModel::AddLayer() {
  if (!draft_) return absl::FailedPreconditionError("No parallax theme draft is open.");
  draft_->layers.push_back({
      .name = absl::StrCat("Layer ", draft_->layers.size() + 1),
      .scroll_factor = {1.0, 1.0},
  });
  selected_layer_ = static_cast<int>(draft_->layers.size()) - 1;
  return absl::OkStatus();
}

absl::Status ParallaxThemeEditorModel::DeleteSelectedLayer() {
  if (!draft_ || !selected_layer_) {
    return absl::FailedPreconditionError("No parallax layer is selected.");
  }
  draft_->layers.erase(draft_->layers.begin() + *selected_layer_);
  ReconcileSelection();
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
  const absl::Status status = ValidateParallaxTheme(request);
  if (!status.ok()) return status;
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
    return;
  }
  if (!selected_layer_) selected_layer_ = 0;
  *selected_layer_ = std::clamp(*selected_layer_, 0, static_cast<int>(draft_->layers.size()) - 1);
}

}  // namespace zebes
