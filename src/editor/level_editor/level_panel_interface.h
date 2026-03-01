#pragma once

#include <optional>

#include "absl/status/status.h"
#include "editor/level_editor/level_selection_state.h"
#include "objects/level.h"

namespace zebes {

class LevelPanelInterface {
 public:
  virtual ~LevelPanelInterface() = default;

  virtual void RefreshCache() = 0;

  virtual absl::Status RenderList(std::optional<Level>& level, SelectionState& selection) = 0;

  virtual absl::Status RenderDetails(std::optional<Level>& level, SelectionState& selection) = 0;
};

}  // namespace zebes
