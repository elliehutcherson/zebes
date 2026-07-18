#pragma once

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/blueprint_editor/sprite_panel_model.h"
#include "editor/canvas/canvas_sprite.h"
#include "editor/gui_interface.h"
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
  static absl::StatusOr<std::unique_ptr<SpritePanel>> Create(Api* api, GuiInterface* gui);

  // Renders the sprite panel UI.
  // This is the main entry point for the panel's rendering logic.
  absl::StatusOr<SpriteResult> Render();

  absl::StatusOr<bool> RenderCanvas(Canvas& canvas, bool input_allowed);

  absl::Status Attach(const std::string& id);
  void Detach();

  // Test helpers
  struct Counters {
    int render_list = 0;
    int render_details = 0;
  };
  const Counters& GetCounters() const { return counters_; }

 private:
  SpritePanel(Api* api, GuiInterface* gui);

  absl::Status Attach(int i);

  // Refreshes the local cache of sprites from the API.
  void RefreshSpriteCache();

  // Renders the list of sprites.
  absl::StatusOr<SpriteResult> RenderList();

  // Renders the details view for the selected sprite.
  absl::StatusOr<SpriteResult> RenderDetails();

  // Renders details for the model's current frame.
  void RenderFrameDetails();

  // Confirms changes to the sprite (update).
  absl::Status ConfirmState();

  Counters counters_;
  SpritePanelModel model_;
  std::unique_ptr<CanvasSprite> canvas_sprite_;

  // Outside dependencies
  Api& api_;
  GuiInterface* gui_;
};

}  // namespace zebes
