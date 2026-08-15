#pragma once

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "objects/texture.h"

namespace zebes {

struct TexturePreviewSize {
  float width = 0.0f;
  float height = 0.0f;
};

// Owns TextureEditor's authoring state and preview calculations without
// depending on ImGui, SDL, or the application API.
class TextureEditorModel {
 public:
  void SetTextures(std::vector<Texture> textures);
  const std::vector<Texture>& textures() const { return textures_; }

  void BeginNewTexture();
  void SelectTexture(Texture texture);
  // Leaves nothing selected -- neither an existing texture nor a new one.
  //
  // Deletion is the only thing that reaches this state. Every other path moves
  // from one texture to another, which is why the model starts with a selection
  // it can hand back and never needed to drop one until now.
  void ClearSelection();
  bool is_new_texture() const { return is_new_texture_; }
  bool has_selection() const;

  Texture* selected_texture();
  const Texture* selected_texture() const;
  void SetSelectedPath(std::string path);

  std::string& edit_name_buffer() { return edit_name_buffer_; }
  const std::string& edit_name_buffer() const { return edit_name_buffer_; }

  absl::StatusOr<Texture> BuildTextureForCreate() const;
  absl::StatusOr<Texture> BuildTextureForUpdate() const;
  void FinishCreate(Texture texture);
  void FinishUpdate();

  float zoom() const { return zoom_; }
  void ZoomIn();
  void ZoomOut();
  void AdjustZoom(float factor);
  void ResetZoom() { zoom_ = 1.0f; }

  TexturePreviewSize CalculatePreviewSize(int texture_width, int texture_height) const;

  // The last failure, shown in the tab until the user dismisses it or the next
  // attempt succeeds.
  //
  // Every failure path here used to be a LOG(ERROR) and nothing else, so an
  // import that failed looked exactly like one that worked unless you happened
  // to be watching a terminal. The message lives on the model rather than the
  // view so the failure logic is testable without SDL or ImGui.
  void SetError(absl::string_view message);
  void ClearError();
  const std::optional<std::string>& error() const { return error_; }

 private:
  void SetEditName(const std::string& name);
  static float ClampZoom(float zoom);

  std::vector<Texture> textures_;
  Texture selected_texture_;
  bool is_new_texture_ = false;
  std::string edit_name_buffer_;
  std::optional<std::string> error_;
  float zoom_ = 1.0f;
};

}  // namespace zebes
