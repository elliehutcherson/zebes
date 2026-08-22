#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/viewport_renderer.h"
#include "editor/parallax_theme_editor/parallax_diagnostics.h"
#include "editor/parallax_theme_editor/parallax_preview_model.h"
#include "editor/parallax_theme_editor/parallax_theme_editor_model.h"
#include "editor/texture_preview.h"

namespace zebes {

class ParallaxThemeEditor {
 public:
  static absl::StatusOr<std::unique_ptr<ParallaxThemeEditor>> Create(Api* api, GuiInterface* gui);

  absl::Status Render();
  absl::Status OpenTheme(const std::string& theme_id);

 private:
  ParallaxThemeEditor(Api* api, GuiInterface* gui);
  absl::Status Save();
  absl::Status Duplicate();
  absl::Status RenderToolbar(ParallaxTheme& draft);
  absl::Status RenderLibrary(ParallaxTheme& draft);
  absl::Status RenderInspector(ParallaxTheme& draft, const std::vector<Texture>& textures);
  absl::Status RenderViewport(const ParallaxTheme& draft, const std::vector<Level>& levels);
  absl::Status RenderDiagnostics(const ParallaxTheme& draft, const std::vector<Level>& levels);
  absl::Status RenderTexturePicker(ParallaxLayer& layer, const std::vector<Texture>& textures);
  absl::Status AnalyzeSelectedTexture();
  void SetError(const absl::Status& status);

  struct PreviewContext {
    CameraCenterRoute route;
    std::optional<CameraWorldBounds> world;
  };

  PreviewContext RenderContextPicker(const ParallaxTheme& draft, const std::vector<Level>& levels);

  struct DiagnosticsSnapshot {
    std::string texture_id;
    RepetitionDiagnostics repetition;
  };

  Api* api_;
  GuiInterface* gui_;
  TexturePreviewRenderer texture_preview_;
  Canvas preview_canvas_;
  ViewportRenderer viewport_renderer_;
  Camera preview_camera_;
  ParallaxThemeEditorModel model_;
  ConfirmPrompt delete_prompt_;
  std::optional<std::string> error_;
  std::optional<DiagnosticsSnapshot> diagnostics_;
  std::string texture_search_;
  std::string context_level_id_;
  std::optional<int> context_zone_id_;
  bool manual_context_ = false;
  Vec manual_route_min_;
  Vec manual_route_max_;
  float travel_x_ = 0.0f;
  float travel_y_ = 0.0f;
  float preview_zoom_ = 1.0f;
  bool preview_selected_layer_ = false;
  bool show_diagnostics_ = true;
};

}  // namespace zebes
