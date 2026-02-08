#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
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

  // Renders the parallax theme panel UI.
  absl::StatusOr<ParallaxThemeResult> Render(Level& level);

  // Returns the texture ID being currently edited/selected, if any.
  std::optional<std::string> GetTexture() const;

 private:
  friend class ParallaxThemePanelTestPeer;

  explicit ParallaxThemePanel(Options options);

  enum class State {
    kThemeList,
    kLayerList,
    kLayerDetails,
  };

  // Internal operations
  enum class Op {
    kCreateTheme,
    kDeleteTheme,
    kEditTheme,
    kBackToThemes,
    kRenameTheme,

    kCreateLayer,
    kDeleteLayer,
    kEditLayer,
    kBackToLayers,

    kSaveLayer,
    kChangeLayerTexture,
  };

  absl::Status RenderThemeList(Level& level);
  absl::Status RenderLayerList(Level& level);
  absl::Status RenderLayerDetails(Level& level);

  absl::Status HandleOp(Level& level, Op op);
  absl::Status RefreshTextureCache();

  Api& api_;
  GuiInterface* gui_;

  State state_ = State::kThemeList;

  // Selections
  std::string selected_theme_name_;
  std::string editing_theme_name_;
  int selected_layer_index_ = -1;

  // Editing state
  std::optional<ParallaxLayer> editing_layer_;
  int selected_texture_index_ = -1;
  std::vector<Texture> texture_cache_;
  std::string error_;
};

}  // namespace zebes
