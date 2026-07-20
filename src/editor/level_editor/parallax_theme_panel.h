#pragma once

#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "editor/texture_preview.h"
#include "objects/level.h"
#include "objects/texture.h"

namespace zebes {

class ParallaxThemePanel {
 public:
  struct Options {
    Api* api;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<ParallaxThemePanel>> Create(Options options);

  // Renders the Tree View in the Navigator Panel
  absl::Status RenderNavigator(Level& level, SelectionState& selection);

  // Renders Inspector for a selected Theme
  absl::Status RenderThemeDetails(Level& level, SelectionState& selection);

  // Renders Inspector for a selected Layer
  absl::Status RenderLayerDetails(Level& level, SelectionState& selection);

 private:
  friend class ParallaxThemePanelTestPeer;

  explicit ParallaxThemePanel(Options options);

  void AddTheme(Level& level, SelectionState& selection);

  absl::Status RefreshTextureCache();

  Api& api_;
  GuiInterface* gui_;
  TexturePreviewRenderer texture_preview_;

  std::vector<Texture> texture_cache_;
};

}  // namespace zebes
