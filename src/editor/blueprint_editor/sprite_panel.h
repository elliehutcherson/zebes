#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/canvas/canvas_sprite.h"
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
  absl::StatusOr<SpriteResult> Render();

  absl::StatusOr<bool> RenderCanvas(Canvas& canvas, bool input_allowed);

  absl::Status Attach(const std::string& id);
  void Detach();

  // Test helpers
  // Test helpers
  struct Counters {
    int render_list = 0;
    int render_details = 0;
  };
  const Counters& GetCounters() const { return counters_; }

 private:
  SpritePanel(Api* api);

  absl::Status Attach(int i);

  // Refreshes the local cache of sprites from the API.
  void RefreshSpriteCache();

  // Renders the list of sprites.
  absl::StatusOr<SpriteResult> RenderList();

  // Renders the details view for the selected sprite.
  absl::StatusOr<SpriteResult> RenderDetails();

  // Renders the details of a specific frame.
  // Assumes frame_index is valid and sdl_texture is populated.
  void RenderFrameDetails(int frame_index);

  // Calculates UV coordinates for a frame.
  std::pair<ImVec2, ImVec2> GetFrameUVs(const SpriteFrame& frame, int tex_w, int tex_h) const;

  // Confirms changes to the sprite (update).
  absl::Status ConfirmState();

  int sprite_index_ = -1;
  int frame_index_ = 0;
  Counters counters_;
  std::vector<Sprite> sprite_cache_;
  std::optional<Sprite> editting_sprite_;
  std::unique_ptr<CanvasSprite> canvas_sprite_;

  // Outside dependencies
  Api& api_;
};

}  // namespace zebes