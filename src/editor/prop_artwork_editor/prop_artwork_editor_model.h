#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "artwork/prepare_prop_asset.h"
#include "artwork/regenerate_prop_asset.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "editor/prop_artwork_editor/prop_artwork_context.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

enum class PropPreviewPolicy : uint8_t {
  kReviewEachStage = 0,
  kFinishedOnly = 1,
};

enum class PropPreviewStage : uint8_t {
  kSource = 0,
  kIsolation = 1,
  kComposition = 2,
  kRasterization = 3,
  kQuantization = 4,
  kEdgeTreatment = 5,
  kCleanup = 6,
  kContext = 7,
};

struct PropPreviewAnchor {
  int x = 0;
  int y = 0;
};

// Platform-neutral authoring state. Panels mutate settings and report intents;
// the containing editor performs filesystem, Api, worker, and GPU operations.
class PropArtworkEditorModel {
 public:
  std::string& name() { return name_; }
  const std::string& name() const { return name_; }
  std::string& source_to_open() { return source_to_open_; }
  std::string& recipe_to_open() { return recipe_to_open_; }

  const std::optional<SourceArtwork>& source() const { return source_; }
  const std::optional<RgbaImage>& source_pixels() const { return source_pixels_; }
  const std::optional<TerrainRecipe>& terrain_recipe() const { return terrain_recipe_; }
  const std::optional<PropRecipe>& active_recipe() const { return active_recipe_; }

  PropRegenerationSettings& settings() { return settings_; }
  const PropRegenerationSettings& settings() const { return settings_; }
  bool has_style() const { return has_style_; }

  uint64_t revision() const { return revision_; }
  void MarkInputsChanged();

  absl::Status SelectSource(SourceArtwork source, RgbaImage pixels);
  absl::Status AttachTerrain(const TerrainRecipe& terrain_recipe);
  void DetachTerrain();
  absl::Status LoadRecipe(const PropRecipe& recipe, SourceArtwork source, RgbaImage pixels,
                          std::optional<TerrainRecipe> terrain_recipe);
  void StartNewRecipe();
  void StartRecipeCopy();

  bool CanPrepare() const;
  bool HasPreparedResult() const;
  const PreparedPropAsset* prepared_creation() const;
  const PreparedPropRegeneration* prepared_regeneration() const;
  absl::Status AcceptPrepared(uint64_t revision, PreparedPropAsset prepared,
                              std::optional<PropArtworkContextPreview> context_preview);
  absl::Status AcceptPrepared(uint64_t revision, PreparedPropRegeneration prepared,
                              std::optional<PropArtworkContextPreview> context_preview);
  void BindCommittedRecipe(const PropRecipe& recipe);

  PropPreviewPolicy preview_policy() const { return preview_policy_; }
  void SetPreviewPolicy(PropPreviewPolicy policy);
  PropPreviewStage preview_stage() const { return preview_stage_; }
  void SetPreviewStage(PropPreviewStage stage) { preview_stage_ = stage; }
  void PreviousPreviewStage();
  void NextPreviewStage();
  const RgbaImage* PreviewImage() const;
  std::optional<PropPreviewAnchor> PreviewAnchor() const;
  const PropStageDiagnostic* PreviewDiagnostic() const;

  void SetStatus(std::string status) { status_ = std::move(status); }
  void ClearStatus() { status_.clear(); }
  const std::string& status() const { return status_; }

 private:
  using PreparedResult = std::variant<std::monostate, PreparedPropAsset, PreparedPropRegeneration>;

  void ClearPrepared();
  const PropArtworkPipelineResult* Artwork() const;

  std::string name_ = "prop";
  std::string source_to_open_;
  std::string recipe_to_open_;
  std::optional<SourceArtwork> source_;
  std::optional<RgbaImage> source_pixels_;
  std::optional<TerrainRecipe> terrain_recipe_;
  std::optional<PropRecipe> active_recipe_;
  PropRegenerationSettings settings_;
  bool has_style_ = false;
  uint64_t revision_ = 1;

  PreparedResult prepared_;
  std::optional<PropArtworkContextPreview> context_preview_;
  PropPreviewPolicy preview_policy_ = PropPreviewPolicy::kFinishedOnly;
  PropPreviewStage preview_stage_ = PropPreviewStage::kContext;
  std::string status_;
};

const char* PropPreviewStageLabel(PropPreviewStage stage);

}  // namespace zebes
