#pragma once

#include <memory>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "editor/preview_texture_sink.h"
#include "editor/terrain_editor/terrain_controls_panel.h"
#include "editor/terrain_editor/terrain_editor_model.h"
#include "editor/terrain_editor/terrain_output_panel.h"
#include "objects/camera.h"

namespace zebes {

// Authors a blob-47 terrain and the tileset that carries it.
//
// It is its own tab rather than a section of the Tileset Editor because tuning
// a terrain is a picture-first activity: the controls only mean anything next
// to a preview large enough to judge, and the Tileset Editor's navigator column
// is a fifth of the window. Here the preview gets the viewport.
//
// The tab produces a finished, saved tileset, so nothing has to exist before
// you start and nothing is left unsaved when you finish.
class TerrainEditor {
 public:
  // preview owns the GPU texture the scene is drawn into.
  static absl::StatusOr<std::unique_ptr<TerrainEditor>> Create(Api* api, GuiInterface* gui,
                                                               PreviewTextureSink* preview);

  ~TerrainEditor() = default;

  absl::Status Render();

 private:
  TerrainEditor(Api* api, GuiInterface* gui, PreviewTextureSink* preview);

  absl::Status Init();

  absl::Status RenderControls();
  absl::Status RenderViewport();
  absl::Status RenderOutput();

  // Runs the creation routine for the model's current source and records where
  // the assets landed.
  void CreateTerrain();
  void OpenRecipe();
  void RegenerateTerrain();

  // Centres the camera on the preview at a zoom that shows all of it with room
  // to spare. Called on the first preview and on demand, never every frame:
  // re-framing while the user is panning would fight them.
  void FrameScene(const ImVec2& viewport_size);

  Api* api_;
  GuiInterface* gui_;
  PreviewTextureSink* preview_;

  Canvas canvas_;
  Camera camera_;
  bool frame_pending_ = true;

  TerrainEditorModel model_;
  std::unique_ptr<TerrainControlsPanel> controls_panel_;
  std::unique_ptr<TerrainOutputPanel> output_panel_;
  std::optional<std::string> error_message_;
};

}  // namespace zebes
