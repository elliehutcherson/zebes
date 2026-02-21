#pragma once

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "editor/level_editor/level_selection_state.h"
#include "objects/level.h"
#include "objects/texture.h"

namespace zebes {

// Result of the Render call.
enum class ParallaxThemeResult : uint8_t {
  kNone,  // Just viewing lists
  kEdit   // Currently editing a specific layer's properties
};

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

  // Returns the texture ID being currently edited/selected, if any.
  std::optional<std::string> GetTexture(const SelectionState& selection, const Level& level) const;

 private:
  friend class ParallaxThemePanelTestPeer;

  explicit ParallaxThemePanel(Options options);

  enum class State {
    kNavigator,
    kThemeDetails,
    kLayerDetails,
  };

  void AddTheme(Level& level, SelectionState& selection);

  absl::Status RefreshTextureCache();

  Api& api_;
  GuiInterface* gui_;

  std::vector<Texture> texture_cache_;
  std::string error_;
};

}  // namespace zebes
