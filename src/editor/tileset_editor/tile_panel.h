#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "editor/gui_interface.h"
#include "objects/tileset.h"

namespace zebes {

// Renders the editable properties of a single Tile in the inspector column.
// Stateless — callers own the Tile and pass it by reference each frame.
class TilePanel {
 public:
  static absl::StatusOr<std::unique_ptr<TilePanel>> Create(GuiInterface* gui);

  ~TilePanel() = default;

  // Renders all editable fields for the given tile: name, source coordinates,
  // collision shape, one-way flag, and comma-separated gameplay tags.
  absl::Status RenderDetails(Tile& tile);

 private:
  explicit TilePanel(GuiInterface* gui);

  GuiInterface* gui_;
};

}  // namespace zebes
