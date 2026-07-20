#pragma once

#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "engine/texture_handle.h"

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

// UI adapter that resolves managed textures and emits native preview images.
class TexturePreviewRenderer {
 public:
  explicit TexturePreviewRenderer(GuiInterface& gui) : gui_(gui) {}

  absl::StatusOr<TexturePreviewLayout> Render(TextureHandle texture, float max_width,
                                              float max_height) const;

 private:
  GuiInterface& gui_;
};

}  // namespace zebes
