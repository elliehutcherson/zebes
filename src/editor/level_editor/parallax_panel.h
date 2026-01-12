#pragma once

#include "absl/status/statusor.h"
#include "api/api.h"
#include "objects/level.h"
#include "objects/texture.h"

namespace zebes {

enum class ParallaxResult : uint8_t { kList, kEdit };

struct ParallaxCounters {
  int render_list = 0;
  int render_details = 0;
  int create = 0;
  int edit = 0;
  int del = 0;
  int save = 0;
  int back = 0;
  int texture = 0;
};

class IParallaxPanel {
 public:
  ~IParallaxPanel() = default;
  virtual absl::StatusOr<ParallaxResult> Render(Level& level) = 0;
  virtual std::optional<std::string> GetTexture() const = 0;
};

class ParallaxPanel : public IParallaxPanel {
 public:
  struct Options {
    Api* api;
  };

  static absl::StatusOr<std::unique_ptr<ParallaxPanel>> Create(Options options);

  // Renders the parallax panel UI.
  // Returns kEdit if editing a layer, kList otherwise.
  absl::StatusOr<ParallaxResult> Render(Level& level) override;

  enum Op : uint8_t {
    kParallaxCreate,
    kParallaxEdit,
    kParallaxSave,
    kParallaxDelete,
    kParallaxBack,
    kParallaxTexture,
  };

  absl::Status HandleOp(Level& level, Op op);

  std::optional<ParallaxLayer>& GetEditingLayer() { return editing_layer_; }
  int GetSelectedIndex() const { return selected_index_; }
  std::optional<std::string> GetTexture() const override;

 private:
  friend class ParallaxPanelTestPeer;

  explicit ParallaxPanel(Options options);

  // Renders the list of parallax layers and CRUD buttons.
  absl::Status RenderList(Level& level);

  // Renders the details view for creating or editing a layer.
  absl::Status RenderDetails(Level& level);

  absl::Status RefreshTextureCache();

  Api& api_;
  int selected_index_ = -1;
  int selected_texture_index_ = -1;
  std::optional<ParallaxLayer> editing_layer_;
  std::vector<Texture> texture_cache_;
  ParallaxCounters counters_;
};

}  // namespace zebes
