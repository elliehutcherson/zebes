#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "objects/level.h"
#include "objects/texture.h"

namespace zebes {

struct ParallaxResult {
  enum Type : uint8_t { kNone = 0, kChanged = 1 };
  Type type = Type::kNone;
};

class ParallaxPanel {
 public:
  struct Options {
    Api* api;
  };

  static absl::StatusOr<std::unique_ptr<ParallaxPanel>> Create(Options options);
  ~ParallaxPanel() = default;

  // Renders the parallax panel UI.
  // Returns kChanged if the level was modified.
  absl::StatusOr<ParallaxResult> Render(Level& level);

  enum Op : uint8_t { kLayerCreate, kLayerEdit, kLayerUpdate, kLayerDelete };

  absl::StatusOr<ParallaxResult> HandleOp(Level& level, Op op);

  std::optional<ParallaxLayer>& GetEditingLayer() { return editing_layer_; }
  int GetSelectedIndex() const { return selected_index_; }

 private:
  explicit ParallaxPanel(Options options);

  // Renders the list of parallax layers and CRUD buttons.
  absl::StatusOr<ParallaxResult> RenderList(Level& level);

  // Renders the details view for creating or editing a layer.
  absl::StatusOr<ParallaxResult> RenderDetails(Level& level);

  absl::Status RefreshTextureCache();

  Api& api_;
  int selected_index_ = -1;
  std::optional<ParallaxLayer> editing_layer_;
  std::vector<Texture> texture_cache_;
};

}  // namespace zebes
