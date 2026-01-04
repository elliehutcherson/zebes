#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "common/sdl_wrapper.h"

namespace zebes {

class LevelEditor {
 public:
  // Creates a new instance of the LevelEditor.
  // Returns an error if the API pointer is null.
  static absl::StatusOr<std::unique_ptr<LevelEditor>> Create(Api* api);

  ~LevelEditor() = default;

  // Renders the main Level Editor UI.
  // This uses a 3-column layout:
  // - Left: Level List (Management)
  // - Center: Viewport (Visual Editor)
  // - Right: Details (Inspector)
  void Render();

 private:
  explicit LevelEditor(Api* api);

  // Renders the level list and management controls.
  void RenderLeft();

  // Renders the main editing viewport.
  void RenderCenter();

  // Renders the properties/details panel for the selected object.
  void RenderRight();

  Api* api_;
};

}  // namespace zebes
