#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "engine/texture_handle.h"
#include "objects/camera.h"

namespace zebes {

// Source dimensions and aspect-fitted display dimensions for a texture preview.
struct TexturePreviewLayout {
  // Native texture width in pixels.
  int source_width = 0;
  // Native texture height in pixels.
  int source_height = 0;
  // Aspect-fitted UI width in pixels.
  float display_width = 0.0f;
  // Aspect-fitted UI height in pixels.
  float display_height = 0.0f;
};

// Calculates an aspect-preserving preview bounded by max_width and max_height.
absl::StatusOr<TexturePreviewLayout> CalculateTexturePreviewLayout(int source_width,
                                                                   int source_height,
                                                                   float max_width,
                                                                   float max_height);

// Centers an image in a Camera whose viewport dimensions have already been
// installed by Canvas, fitting it with a caller-selected fraction of margin.
// Shared by picture-first editor tabs so framing behavior does not drift.
absl::Status FrameImagePreviewCamera(Camera& camera, int source_width, int source_height,
                                     float fill_fraction, CameraZoomRange zoom_range);

// A managed atlas resolved for draw-list rendering: the ImGui texture ID plus
// the native dimensions callers need to normalize sub-rectangle UVs.
struct AtlasBinding {
  ImTextureID texture_id = 0;
  int width = 0;
  int height = 0;

  bool IsValid() const { return texture_id != 0 && width > 0 && height > 0; }
};

// UI adapter that resolves managed textures and emits native preview images.
class TexturePreviewRenderer {
 public:
  explicit TexturePreviewRenderer(GuiInterface& gui) : gui_(gui) {}

  absl::StatusOr<TexturePreviewLayout> Render(TextureHandle texture, float max_width,
                                              float max_height) const;

  // Resolves a managed atlas for callers that compose their own draw-list
  // geometry, such as a palette sampling individual tile cells. Keeps SDL
  // resolution and dimension queries inside this boundary adapter.
  //
  // An invalid handle is an ordinary authoring state, not an error: it yields a
  // default-constructed binding so callers can draw a placeholder.
  static absl::StatusOr<AtlasBinding> BindAtlas(TextureHandle texture);

 private:
  GuiInterface& gui_;
};

}  // namespace zebes
