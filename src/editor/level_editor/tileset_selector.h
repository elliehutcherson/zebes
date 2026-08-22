#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace zebes {

class Api;
class GuiInterface;
struct Tileset;

struct TilesetSelectorResult {
  Tileset* tileset = nullptr;
  bool selection_changed = false;
  bool catalog_empty = false;
};

// Shared stable-ID selector for palette panels that operate on a tileset.
// Resource pointers are resolved for the current frame and are never retained
// as the selector's authoritative state.
class TilesetSelector {
 public:
  absl::StatusOr<TilesetSelectorResult> Render(Api& api, GuiInterface& gui, const char* combo_label,
                                               std::string_view item_id_prefix);

  void Select(std::string tileset_id);
  void Clear() { selected_id_.reset(); }
  const std::optional<std::string>& selected_id() const { return selected_id_; }

 private:
  std::optional<std::string> selected_id_;
};

}  // namespace zebes
