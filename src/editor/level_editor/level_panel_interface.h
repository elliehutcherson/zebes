#pragma once

#include <cstdint>

#include "absl/status/statusor.h"
#include "editor/level_editor/level_panel_model.h"

namespace zebes {

enum class LevelPanelAction : std::uint8_t {
  kNone,
  kCreate,
  kOpen,
  kSave,
  kDelete,
  kClose,
};

struct LevelPanelEvent {
  LevelPanelAction action = LevelPanelAction::kNone;
};

class LevelPanelInterface {
 public:
  virtual ~LevelPanelInterface() = default;

  virtual absl::StatusOr<LevelPanelEvent> RenderList(LevelPanelModel& model) = 0;
  virtual absl::StatusOr<LevelPanelEvent> RenderDetails(LevelPanelModel& model) = 0;
};

}  // namespace zebes
