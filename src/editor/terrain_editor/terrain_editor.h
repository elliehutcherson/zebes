#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/background_task.h"
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
  // Reaches the actions the output panel reports, so what the editor does with
  // an Api refusal can be driven without a window.
  friend class TerrainEditorTestPeer;

  // preview owns the GPU texture the scene is drawn into.
  static absl::StatusOr<std::unique_ptr<TerrainEditor>> Create(Api* api, GuiInterface* gui,
                                                               PreviewTextureSink* preview);

  // An in-flight worker owns only copied configuration and platform-neutral
  // output. Destroying the editor waits for it before those members disappear.
  ~TerrainEditor() = default;

  absl::Status Render();

 private:
  TerrainEditor(Api* api, GuiInterface* gui, PreviewTextureSink* preview);

  absl::Status Init();

  absl::Status RenderControls();
  absl::Status RenderViewport();
  absl::Status RenderOutput();

  // Runs the creation routine for the model's current source and records where
  // the assets landed. Generated artwork starts a worker; imported artwork is
  // already present and commits immediately.
  void CreateTerrain();
  void PollTerrainWork();
  bool HasPendingTerrainWork() const;
  void OpenRecipe();
  void RegenerateTerrain();
  // Removes the open terrain whole -- recipe, tileset and artwork -- and
  // returns the tab to its empty state, since what it was editing is gone.
  void DeleteTerrain();

  // Centres the camera on the preview at a zoom that shows all of it with room
  // to spare. Called on the first preview and on demand, never every frame:
  // re-framing while the user is panning would fight them.
  absl::Status FrameScene(const ImVec2& viewport_size);

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

  struct PendingCreation {
    std::string name;
    TerrainGenConfig config;
    std::optional<std::string> source_preset;
    BackgroundTask<PreparedGeneratedTerrain> work;
  };

  struct PendingRegeneration {
    TerrainRecipe recipe;
    TerrainGenConfig config;
    BackgroundTask<PreparedTerrainRegeneration> work;
  };

  // Api is deliberately absent: workers render copied inputs, and
  // PollTerrainWork performs every resource mutation here.
  std::variant<std::monostate, PendingCreation, PendingRegeneration> pending_work_;
};

}  // namespace zebes
