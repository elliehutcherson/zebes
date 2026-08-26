#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/background_task.h"
#include "editor/canvas/canvas.h"
#include "editor/gui_interface.h"
#include "editor/pointer_drag.h"
#include "editor/preview_texture_sink.h"
#include "editor/prop_artwork_editor/prop_artwork_context.h"
#include "editor/prop_artwork_editor/prop_artwork_controls_panel.h"
#include "editor/prop_artwork_editor/prop_artwork_editor_model.h"
#include "editor/prop_artwork_editor/prop_artwork_output_panel.h"
#include "generation/image_generation_request_controller.h"
#include "objects/camera.h"

namespace zebes {

struct PreparedPropCreationPreview {
  PreparedPropAsset asset;
  std::optional<PropArtworkContextPreview> context;
};

struct PreparedPropRegenerationPreview {
  PreparedPropRegeneration asset;
  std::optional<PropArtworkContextPreview> context;
};

// Every dependency is borrowed and must outlive the editor. Generation
// providers are session-lifetime engines owned by the composition root.
struct PropArtworkEditorOptions {
  Api* api = nullptr;
  GuiInterface* gui = nullptr;
  PreviewTextureSink* preview = nullptr;
  ImageGenerationProviderRegistry* generation_providers = nullptr;
};

class PropArtworkEditor {
 public:
  friend class PropArtworkEditorTestPeer;

  static absl::StatusOr<std::unique_ptr<PropArtworkEditor>> Create(
      const PropArtworkEditorOptions& options);
  ~PropArtworkEditor();

  absl::Status Render();

 private:
  explicit PropArtworkEditor(const PropArtworkEditorOptions& options);

  absl::Status Init();
  void StartImport(std::string path);
  void SelectSource();
  void SelectGenerationProvider(size_t index);
  void StartGeneration();
  void CancelGeneration();
  void AcceptCandidate();
  // Drains the generation engine. Separate from PollWork's pending_work_
  // states because a remote generation runs beside local processing rather
  // than instead of it.
  void PollGeneration();
  absl::Status RetainCandidateAsSource(const ImageGenerationReview& review,
                                       const ImageGenerationCandidate& candidate);
  void DeleteSelectedSource();
  void OpenRecipe();
  void ClearWorkspace();
  void StartPreparation();
  void CommitPrepared();
  void DeleteProp();
  void PollWork();
  bool HasPendingWork() const;
  absl::Status DiscardSessionSource();

  absl::Status RenderControls();
  absl::Status RenderPreview();
  absl::Status RenderOutput();
  absl::Status UpdateContextPreviewDrag();

  struct PendingImport {
    std::string path;
    std::string source_name;
    std::string original_filename;
    std::string imported_at_utc;
    BackgroundTask<RgbaImage> work;
  };

  struct PendingCreation {
    uint64_t revision = 0;
    BackgroundTask<PreparedPropCreationPreview> work;
  };

  struct PendingRegeneration {
    uint64_t revision = 0;
    BackgroundTask<PreparedPropRegenerationPreview> work;
  };

  absl::Status FinishImport(PendingImport completed);

  Api* api_;
  GuiInterface* gui_;
  PreviewTextureSink* preview_;
  std::unique_ptr<ImageGenerationRequestController> generation_;
  Canvas preview_canvas_;
  PointerDragController preview_prop_drag_;
  Camera preview_camera_;
  bool frame_pending_ = true;
  uint64_t framed_revision_ = 0;
  PropPreviewStage framed_stage_ = PropPreviewStage::kContext;
  int framed_width_ = 0;
  int framed_height_ = 0;
  bool framed_prepared_ = false;
  PropArtworkEditorModel model_;
  std::unique_ptr<PropArtworkControlsPanel> controls_panel_;
  std::unique_ptr<PropArtworkOutputPanel> output_panel_;
  std::variant<std::monostate, PendingImport, PendingCreation, PendingRegeneration> pending_work_;
  // Import persists pixels so the manager remains the single authority during
  // processing. Until a prop bundle references them, this editor session owns
  // the record and removes it on replacement, Clear, or normal shutdown.
  std::optional<std::string> session_source_id_;
};

}  // namespace zebes
