#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/background_task.h"
#include "editor/canvas/canvas.h"
#include "editor/confirm_prompt.h"
#include "editor/gui_interface.h"
#include "editor/parallax_artwork_editor/parallax_artwork_editor_model.h"
#include "editor/preview_texture_sink.h"
#include "objects/camera.h"

namespace zebes {

struct ParallaxArtworkEditorOptions {
  Api* api = nullptr;
  GuiInterface* gui = nullptr;
  PreviewTextureSink* preview = nullptr;
};

class ParallaxArtworkEditor {
 public:
  friend class ParallaxArtworkEditorTestPeer;

  static absl::StatusOr<std::unique_ptr<ParallaxArtworkEditor>> Create(
      const ParallaxArtworkEditorOptions& options);
  ~ParallaxArtworkEditor();

  absl::Status Render();

 private:
  explicit ParallaxArtworkEditor(const ParallaxArtworkEditorOptions& options);

  void StartImport(std::string path);
  void SelectSource();
  void DeleteSelectedSource();
  void OpenRecipe();
  void ClearWorkspace();
  void StartPreparation();
  void CommitPrepared();
  void DeleteArtwork();
  void PollWork();
  bool HasPendingWork() const;
  absl::Status DiscardSessionSource();

  absl::Status RenderInput();
  absl::Status RenderPreview();
  absl::Status RenderOutput();
  bool RenderPipelineSettings();

  struct PendingImport {
    std::string source_name;
    std::string original_filename;
    std::string imported_at_utc;
    BackgroundTask<RgbaImage> work;
  };

  struct PendingCreation {
    uint64_t revision = 0;
    BackgroundTask<PreparedParallaxArtworkAsset> work;
  };

  struct PendingRegeneration {
    uint64_t revision = 0;
    BackgroundTask<PreparedParallaxArtworkRegeneration> work;
  };

  Api* api_;
  GuiInterface* gui_;
  PreviewTextureSink* preview_;
  Canvas preview_canvas_;
  Camera preview_camera_;
  bool frame_pending_ = true;
  uint64_t framed_revision_ = 0;
  ParallaxArtworkPreviewStage framed_stage_ = ParallaxArtworkPreviewStage::kSource;
  int framed_width_ = 0;
  int framed_height_ = 0;
  ParallaxArtworkEditorModel model_;
  ConfirmPrompt delete_source_prompt_;
  ConfirmPrompt delete_artwork_prompt_;
  ConfirmPrompt clear_prompt_;
  std::variant<std::monostate, PendingImport, PendingCreation, PendingRegeneration> pending_work_;
  std::optional<std::string> session_source_id_;
};

}  // namespace zebes
