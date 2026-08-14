#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "editor/asset_catalog.h"
#include "engine/texture_handle.h"
#include "objects/sprite.h"
#include "objects/texture.h"

namespace zebes {

struct AnimationPreviewLayout {
  int64_t bounds_left = 0;
  int64_t bounds_top = 0;
  int64_t bounds_right = 0;
  int64_t bounds_bottom = 0;
  float scale = 1.0f;
  float canvas_width = 0.0f;
  float canvas_height = 0.0f;
  float frame_x = 0.0f;
  float frame_y = 0.0f;
  float frame_width = 0.0f;
  float frame_height = 0.0f;
};

// Authoring state for SpriteEditor. Catalogs use AssetCatalogKey so duplicate
// names remain representable and UI iteration is deterministic without
// repeated sorting.
class SpriteEditorModel {
 public:
  using SpriteCatalog = std::map<AssetCatalogKey, Sprite>;
  using TextureCatalog = std::map<AssetCatalogKey, Texture>;

  void SetSprites(std::vector<Sprite> sprites);
  void SetTextures(std::vector<Texture> textures);
  const SpriteCatalog& sprites() const { return sprites_; }
  const TextureCatalog& textures() const { return textures_; }

  absl::Status SelectSprite(const std::string& id);
  void BeginNewSprite();
  void ClearSelection();
  bool is_new_sprite() const { return is_new_sprite_; }
  bool has_selection() const { return is_new_sprite_ || !sprite_.id.empty(); }

  Sprite& sprite() { return sprite_; }
  const Sprite& sprite() const { return sprite_; }
  std::string& edit_name_buffer() { return edit_name_buffer_; }
  const std::string& edit_name_buffer() const { return edit_name_buffer_; }

  // GPU handle for sprite_'s texture. Held here rather than on the Sprite,
  // which is a pure definition and names its texture by ID only. Invalid until
  // a texture is selected, and cleared whenever the selection changes.
  TextureHandle texture() const { return texture_; }

  // Drops the resolved handle without touching sprite_.texture_id, so a texture
  // that failed to load renders as absent rather than as stale artwork.
  void ClearTexture() { texture_ = {}; }

  absl::Status SelectTexture(const std::string& texture_id, TextureHandle handle);
  absl::StatusOr<Sprite> BuildCreateRequest() const;
  absl::StatusOr<Sprite> BuildUpdateRequest() const;
  void FinishSave();

  // The last failure, shown in the tab until dismissed or superseded.
  //
  // Every failure path here used to be a LOG(ERROR) and nothing else, so a
  // sprite that failed to save looked exactly like one that saved. The message
  // lives on the model rather than the view so the failure logic is testable
  // without SDL or ImGui.
  void SetError(absl::string_view message);
  void ClearError();
  const std::optional<std::string>& error() const { return error_; }

  int active_frame_index() const { return active_frame_index_; }
  void ToggleActiveFrame(int index);
  absl::Status AddFrame();
  absl::Status DeleteFrame(int index);
  absl::Status MoveFrameLeft(int index);
  absl::Status MoveFrameRight(int index);
  void ResetFrames();
  absl::Status ApplyFrameScale(int index, int scale);
  absl::Status ClampFrameToTexture(int index, int texture_width, int texture_height);

  float texture_zoom() const { return texture_zoom_; }
  void ZoomTextureIn();
  void ZoomTextureOut();

  static absl::StatusOr<AnimationPreviewLayout> CalculateAnimationPreviewLayout(
      const std::vector<SpriteFrame>& frames, int current_frame_index, float available_width,
      float maximum_height);

 private:
  void SetEditName(const std::string& name);
  void ReindexFrames();
  bool IsValidFrameIndex(int index) const;

  SpriteCatalog sprites_;
  TextureCatalog textures_;
  Sprite sprite_;
  TextureHandle texture_;
  bool is_new_sprite_ = false;
  std::string edit_name_buffer_;
  std::optional<std::string> error_;
  std::vector<SpriteFrame> original_frames_;
  int active_frame_index_ = -1;
  float texture_zoom_ = 1.0f;
};

}  // namespace zebes
