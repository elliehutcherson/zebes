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
#include "editor/pointer_drag.h"
#include "editor/texture_preview.h"

namespace zebes {

enum class ParallaxPreviewScope {
  kCompleteTheme,
  kSelectedLayer,
  kSelectedElement,
};

class ParallaxThemeEditor {
 public:
  friend class ParallaxThemeEditorTestPeer;

  static absl::StatusOr<std::unique_ptr<ParallaxThemeEditor>> Create(Api* api, GuiInterface* gui);

  absl::Status Render();
  absl::Status OpenTheme(const std::string& theme_id);

 private:
  ParallaxThemeEditor(Api* api, GuiInterface* gui);
  absl::Status Save();
  absl::Status Duplicate();
  absl::Status RenderToolbar(ParallaxTheme& draft, bool& save_requested);
  absl::Status RenderLayerNavigator(ParallaxTheme& draft);
  absl::Status RenderInspector(ParallaxTheme& draft, const std::vector<Texture>& textures);
  absl::Status RenderViewport(ParallaxTheme& draft, const std::vector<Level>& levels);
  absl::Status RenderDiagnostics(const ParallaxTheme& draft, const std::vector<Level>& levels);
  absl::Status RenderTexturePicker(ParallaxElement& element, const std::vector<Texture>& textures);
  absl::Status AnalyzeSelectedTexture();
  absl::StatusOr<std::vector<ParallaxElementSize>> ResolveElementSizes(const ParallaxLayer& layer);
  absl::Status SetRepeatMode(ParallaxLayer& layer, bool repeat_x, bool repeat_y);
  absl::Status FitRepeatPeriodToContent(ParallaxLayer& layer);
  absl::Status AppendElementRight(ParallaxLayer& layer);
  absl::Status SnapSelectedElement(ParallaxLayer& layer, int direction);
  absl::Status UpdatePreviewElementDrag(ParallaxLayer& editable_layer,
                                        const ParallaxLayer& preview_layer,
                                        const std::vector<ParallaxElementSize>& element_sizes);
  void SetError(const absl::Status& status);
  void CloseTheme();

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
  PointerDragController element_drag_;
  std::optional<int> dragged_element_id_;
  Vec dragged_repeat_offset_;
  Camera preview_camera_;
  ParallaxThemeEditorModel model_;
  ConfirmPrompt close_prompt_;
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
  ParallaxPreviewScope preview_scope_ = ParallaxPreviewScope::kCompleteTheme;
  bool show_diagnostics_ = true;
};

}  // namespace zebes
