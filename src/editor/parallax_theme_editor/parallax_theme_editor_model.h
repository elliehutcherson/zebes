#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "objects/parallax_theme.h"

namespace zebes {

enum class ParallaxDepthPreset {
  kFar,
  kMiddle,
  kNearBackground,
};

// Platform-neutral draft state for the standalone theme editor.
class ParallaxThemeEditorModel {
 public:
  void BeginNew();
  void Open(const ParallaxTheme& theme);
  void Close();
  absl::Status DiscardChanges();

  bool has_draft() const { return draft_.has_value(); }
  bool is_new() const { return has_draft() && draft_->id.empty(); }
  bool dirty() const {
    return draft_.has_value() && (!snapshot_.has_value() || *draft_ != *snapshot_);
  }
  ParallaxTheme* draft() { return draft_ ? &*draft_ : nullptr; }
  const ParallaxTheme* draft() const { return draft_ ? &*draft_ : nullptr; }
  std::optional<int> selected_layer() const { return selected_layer_; }
  std::optional<int> selected_element_id() const { return selected_element_id_; }
  void SelectLayer(int index);
  void SelectElement(int element_id);

  absl::Status AddLayer();
  absl::Status DeleteSelectedLayer();
  absl::Status MoveSelectedLayer(int delta);
  absl::Status AddElement();
  absl::Status AddElementAt(Vec position);
  absl::Status DuplicateSelectedElement();
  absl::Status DeleteSelectedElement();
  absl::Status MoveSelectedElement(int delta);
  absl::Status ApplyDepthPreset(ParallaxDepthPreset preset);
  absl::StatusOr<ParallaxTheme> BuildSaveRequest() const;
  void FinishSave(const std::string& id);

 private:
  void ReconcileSelection();

  std::optional<ParallaxTheme> draft_;
  std::optional<ParallaxTheme> snapshot_;
  std::optional<int> selected_layer_;
  std::optional<int> selected_element_id_;
};

}  // namespace zebes
