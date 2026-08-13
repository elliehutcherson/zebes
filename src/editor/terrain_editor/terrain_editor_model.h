#pragma once

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "editor/terrain_editor/terrain_creation.h"
#include "terrain/blob47_compose.h"
#include "terrain/terrain_generator.h"
#include "terrain/terrain_recipe.h"

namespace zebes {

// Authoring state for the terrain tab: what is being made, and the preview of
// it. Holds no ImGui, so the rules about when a preview is stale and what a
// configuration will produce are testable without a window.
class TerrainEditorModel {
 public:
  // Where a terrain's artwork comes from. Both routes end at the same tileset;
  // they differ only in whether the pixels are drawn here or already exist.
  enum class Source : uint8_t {
    kGenerate = 0,
    kImportManifest = 1,
  };

  // Mutable so panels can bind controls straight to it, as the tileset panel
  // binds to its Tileset. Editing through here does not mark the preview
  // stale -- panels report that themselves, since only they know a control
  // actually moved.
  TerrainGenConfig& config() { return config_; }
  const TerrainGenConfig& config() const { return config_; }

  // Preset identity belongs to the editor, not to the material's display name.
  // Any manual artistic edit clears it; applying a preset preserves output
  // quality and the random seed, which are authoring choices rather than art.
  const std::optional<std::string>& selected_preset() const { return selected_preset_; }
  void ApplyPreset(const TerrainPreset& preset);
  void MarkConfigCustom() { selected_preset_.reset(); }

  // A loaded recipe keeps the immutable asset binding used by Regenerate.
  // Starting a copy deliberately drops that binding so Create allocates fresh
  // IDs instead of overwriting the source.
  void LoadRecipe(const TerrainRecipe& recipe);
  void StartNewRecipe();
  void StartRecipeCopy();
  const std::optional<TerrainRecipe>& active_recipe() const { return active_recipe_; }
  std::string& recipe_to_open() { return recipe_to_open_; }

  Source source() const { return source_; }
  void SetSource(Source source);

  std::string& name() { return name_; }
  const std::string& name() const { return name_; }
  std::string& manifest_path() { return manifest_path_; }
  std::string& texture_id() { return texture_id_; }
  const std::string& texture_id() const { return texture_id_; }

  // How many tiles the current configuration will emit: one per mask per phase,
  // plus one unit per slope shape.
  int TileCount() const;

  void MarkPreviewStale() { preview_stale_ = true; }

  // Redraws the preview when it is out of date.
  //
  // A change redraws immediately at draft quality and settles at full quality
  // on a later frame once nothing is being dragged. Rendering the good version
  // first would stall for around a second on every slider tick, and on the very
  // first frame the tab is opened.
  absl::Status RefreshPreviewIfNeeded(bool interacting);

  const std::optional<RgbaImage>& preview() const { return preview_; }

  void SetStatus(std::string status) { status_ = std::move(status); }
  const std::string& status() const { return status_; }

  // Set after a successful create, so the tab can say where the assets went.
  void SetResult(CreatedTerrain result) { result_ = std::move(result); }
  const std::optional<CreatedTerrain>& result() const { return result_; }

 private:
  absl::Status RefreshPreview(bool draft);

  TerrainGenConfig config_;
  std::optional<std::string> selected_preset_ = "Classic Grass";
  std::optional<TerrainRecipe> active_recipe_;
  std::string recipe_to_open_;
  Source source_ = Source::kGenerate;
  std::string name_ = "terrain";
  std::string manifest_path_;
  std::string texture_id_;

  bool preview_stale_ = true;
  // Set while the current preview is a reduced-quality draft, which is what
  // makes the settling pass happen at all: by then nothing has changed and the
  // preview is no longer stale.
  bool preview_is_draft_ = false;
  std::optional<RgbaImage> preview_;

  std::string status_;
  std::optional<CreatedTerrain> result_;
};

}  // namespace zebes
