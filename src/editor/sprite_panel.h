#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "imgui.h"
#include "objects/sprite.h"

namespace zebes {

struct SpriteResult {
  enum class Type : uint8_t { kNone, kAttach, kDetach };
  Type type = Type::kNone;
  std::string id = "";
};

class SpritePanel {
 public:
  // Creates a new SpritePanel instance.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<SpritePanel>> Create(Api* api);

  // Renders the sprite panel UI.
  // This is the main entry point for the panel's rendering logic.
  SpriteResult Render();

  // Resets the panel state.
  void Reset();

  // Returns the currently selected sprite, or nullptr if none.
  Sprite* GetSprite();

  // Returns the current frame index being edited/viewed.
  int GetFrameIndex() const;

  // Sets the currently selected sprite by ID.
  void SetSprite(const std::string& id);

  // Sets the currently attached sprite ID for the active blueprint state.
  // This determines whether the "Attach" or "Detach" button is shown.
  void SetAttachedSprite(const std::optional<std::string>& id);

 private:
  SpritePanel(Api* api);

  // Refreshes the local cache of sprites from the API.
  void RefreshSpriteCache();

  // Renders the list of sprites.
  SpriteResult RenderList();

  // Renders the details view for the selected sprite.
  SpriteResult RenderDetails();

  // Renders the details of a specific frame.
  // Assumes frame_index is valid and sdl_texture is populated.
  void RenderFrameDetails(int frame_index);

  // Calculates UV coordinates for a frame.
  std::pair<ImVec2, ImVec2> GetFrameUVs(const SpriteFrame& frame, int tex_w, int tex_h) const;

  // Confirms changes to the sprite (update).
  void ConfirmState();

  // Helper to check if the current sprite is attached.
  bool IsAttached() const;

  std::vector<Sprite> sprite_cache_;
  int sprite_index_ = -1;
  std::optional<Sprite> editting_sprite_;
  int current_frame_index_ = 0;
  std::optional<std::string> attached_sprite_id_;

  // Outside dependencies
  Api* api_;
};

}  // namespace zebes
