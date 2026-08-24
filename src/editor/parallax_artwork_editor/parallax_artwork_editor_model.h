#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "artwork/prepare_parallax_artwork_asset.h"
#include "artwork/regenerate_parallax_artwork_asset.h"
#include "artwork/source_artwork.h"
#include "common/image_io.h"
#include "editor/image_generation/artwork_generation_prompts.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

enum class ParallaxArtworkPreviewStage : uint8_t {
  kSource = 0,
  kFraming = 1,
  kMatteExtraction = 2,
  kRasterization = 3,
  kFinished = 4,
  kRepeatX = 5,
  kRepeatY = 6,
};

enum class ParallaxArtworkStatusKind : uint8_t {
  kInfo,
  kReady,
  kSuccess,
  kError,
};

// Platform-neutral state for import-first background artwork authoring. The
// containing editor owns filesystem, Api, worker, and GPU operations.
class ParallaxArtworkEditorModel {
 public:
  std::string& name() { return name_; }
  const std::string& name() const { return name_; }
  std::string& source_to_open() { return source_to_open_; }
  std::string& recipe_to_open() { return recipe_to_open_; }

  const std::optional<SourceArtwork>& source() const { return source_; }
  const std::optional<RgbaImage>& source_pixels() const { return source_pixels_; }
  const std::optional<TerrainRecipe>& terrain_recipe() const { return terrain_recipe_; }
  const std::optional<ParallaxArtworkRecipe>& active_recipe() const { return active_recipe_; }

  ParallaxArtworkRegenerationSettings& settings() { return settings_; }
  const ParallaxArtworkRegenerationSettings& settings() const { return settings_; }
  bool has_style() const { return has_style_; }

  std::string& prompt() { return prompt_; }
  const std::string& prompt() const { return prompt_; }
  std::string& generation_instructions() { return generation_instructions_; }
  const std::string& generation_instructions() const { return generation_instructions_; }
  int requested_candidates() const { return requested_candidates_; }
  void SetRequestedCandidates(int candidates, int maximum);
  ArtworkGenerationStylePreset style_preset() const { return style_preset_; }
  void SetStylePreset(ArtworkGenerationStylePreset preset);
  std::string& style_guidance() { return style_guidance_; }
  const std::string& style_guidance() const { return style_guidance_; }
  void MarkStyleGuidanceCustom() { style_preset_ = ArtworkGenerationStylePreset::kCustom; }

  uint64_t revision() const { return revision_; }
  void MarkInputsChanged();

  absl::Status SelectSource(SourceArtwork source, RgbaImage pixels);
  absl::Status AttachTerrain(const TerrainRecipe& terrain_recipe);
  void DetachTerrain();
  absl::Status LoadRecipe(const ParallaxArtworkRecipe& recipe, SourceArtwork source,
                          RgbaImage pixels, std::optional<TerrainRecipe> terrain_recipe);
  void StartNewRecipe();
  void StartRecipeCopy();

  bool CanPrepare() const;
  bool HasPreparedResult() const;
  bool HasUncommittedPreparedResult() const;
  const PreparedParallaxArtworkAsset* prepared_creation() const;
  const PreparedParallaxArtworkRegeneration* prepared_regeneration() const;
  absl::Status AcceptPrepared(uint64_t revision, PreparedParallaxArtworkAsset prepared);
  absl::Status AcceptPrepared(uint64_t revision, PreparedParallaxArtworkRegeneration prepared);
  void BindCommittedRecipe(const ParallaxArtworkRecipe& recipe);

  ParallaxArtworkPreviewStage preview_stage() const { return preview_stage_; }
  void SetPreviewStage(ParallaxArtworkPreviewStage stage);
  void PreviousPreviewStage();
  void NextPreviewStage();
  const RgbaImage* PreviewImage() const;
  const ParallaxArtworkStageDiagnostic* PreviewDiagnostic() const;
  const RepetitionDiagnostics* repetition_diagnostics() const;

  void SetInfo(std::string status) { SetStatus(ParallaxArtworkStatusKind::kInfo, status); }
  void SetReady(std::string status) { SetStatus(ParallaxArtworkStatusKind::kReady, status); }
  void SetSuccess(std::string status) { SetStatus(ParallaxArtworkStatusKind::kSuccess, status); }
  void SetError(std::string status) { SetStatus(ParallaxArtworkStatusKind::kError, status); }
  void ClearStatus() { status_.clear(); }
  const std::string& status() const { return status_; }
  ParallaxArtworkStatusKind status_kind() const { return status_kind_; }

 private:
  using PreparedResult = std::variant<std::monostate, PreparedParallaxArtworkAsset,
                                      PreparedParallaxArtworkRegeneration>;

  void ClearPrepared();
  void SetStatus(ParallaxArtworkStatusKind kind, std::string status) {
    status_kind_ = kind;
    status_ = std::move(status);
  }
  const ParallaxArtworkPipelineResult* Artwork() const;
  std::vector<ParallaxArtworkPreviewStage> AvailablePreviewStages() const;

  std::string name_ = "background";
  std::string source_to_open_;
  std::string recipe_to_open_;
  std::optional<SourceArtwork> source_;
  std::optional<RgbaImage> source_pixels_;
  std::optional<TerrainRecipe> terrain_recipe_;
  std::optional<ParallaxArtworkRecipe> active_recipe_;
  ParallaxArtworkRegenerationSettings settings_;
  bool has_style_ = false;
  std::string prompt_;
  std::string generation_instructions_ = kDefaultParallaxGenerationInstructions;
  ArtworkGenerationStylePreset style_preset_ = ArtworkGenerationStylePreset::kCustom;
  std::string style_guidance_;
  int requested_candidates_ = 1;
  uint64_t revision_ = 1;
  PreparedResult prepared_;
  bool prepared_committed_ = false;
  ParallaxArtworkPreviewStage preview_stage_ = ParallaxArtworkPreviewStage::kSource;
  ParallaxArtworkStatusKind status_kind_ = ParallaxArtworkStatusKind::kInfo;
  std::string status_;
};

const char* ParallaxArtworkPreviewStageLabel(ParallaxArtworkPreviewStage stage);

}  // namespace zebes
