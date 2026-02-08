#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/api.h"
#include "editor/gui_interface.h"
#include "objects/level.h"

namespace zebes {

// Result of the Render call.
enum class ParallaxZoneResult : uint8_t {
  kNone,  // Just viewing lists
  kEdit   // Currently editing a specific zone's properties
};

class ParallaxZonePanel {
 public:
  struct Options {
    Api* api;
    GuiInterface* gui = nullptr;
  };

  static absl::StatusOr<std::unique_ptr<ParallaxZonePanel>> Create(Options options);

  // Renders the parallax zone panel UI.
  absl::StatusOr<ParallaxZoneResult> Render(Level& level);

 private:
  friend class ParallaxZonePanelTestPeer;

  explicit ParallaxZonePanel(Options options);

  enum class State {
    kZoneList,
    kZoneDetails,
  };

  // Internal operations
  enum class Op {
    kCreateZone,
    kDeleteZone,
    kEditZone,
    kBackToZones,
    kSaveZone,
  };

  absl::Status RenderZoneList(Level& level);
  absl::Status RenderZoneDetails(Level& level);

  absl::Status HandleOp(Level& level, Op op);

  Api& api_;
  GuiInterface* gui_;

  State state_ = State::kZoneList;

  // Selections
  int selected_zone_index_ = -1;

  // Editing state
  std::optional<ParallaxZone> editing_zone_;
  std::string error_;
};

}  // namespace zebes
