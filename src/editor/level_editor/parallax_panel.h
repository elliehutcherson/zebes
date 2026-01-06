#pragma once

#include "absl/status/statusor.h"
#include "objects/level.h"

namespace zebes {

struct ParallaxResult {
  enum Type : uint8_t { kNone = 0, kChanged = 1 };
  Type type = Type::kNone;
};

class ParallaxPanel {
 public:
  ParallaxPanel() = default;
  ~ParallaxPanel() = default;

  // Renders the parallax panel UI.
  // Returns kChanged if the level was modified.
  absl::StatusOr<ParallaxResult> Render(Level& level);

 private:
  enum Op : uint8_t { kLayerCreate, kLayerUpdate, kLayerDelete };

  // Renders the list of parallax layers and CRUD buttons.
  absl::StatusOr<ParallaxResult> RenderList(Level& level);

  // Renders the details view for creating or editing a layer.
  absl::StatusOr<ParallaxResult> RenderDetails(Level& level);

  absl::Status ConfirmState(Level& level, Op op);

  int selected_index_ = -1;
  std::optional<ParallaxLayer> editing_layer_;
};

}  // namespace zebes
