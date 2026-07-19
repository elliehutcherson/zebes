#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "absl/status/statusor.h"
#include "editor/blueprint_editor/collider_panel_model.h"
#include "editor/canvas/canvas_collider.h"
#include "editor/gui_interface.h"

namespace zebes {

// Renders collider authoring state and reports persistence intents to the
// containing editor. It does not access the application API.
class ColliderPanel {
 public:
  enum class Action : std::uint8_t {
    kNone,
    kCreate,
    kAttach,
    kSave,
    kDelete,
    kDetach,
  };

  static absl::StatusOr<std::unique_ptr<ColliderPanel>> Create(GuiInterface* gui);

  absl::StatusOr<Action> Render(ColliderPanelModel& model);
  absl::StatusOr<bool> RenderCanvas(ColliderPanelModel& model, Canvas& canvas,
                                    bool input_allowed);

 private:
  explicit ColliderPanel(GuiInterface* gui);

  absl::StatusOr<Action> RenderList(ColliderPanelModel& model);
  absl::StatusOr<Action> RenderDetails(ColliderPanelModel& model);
  absl::Status RenderPolygonList(ColliderPanelModel& model);
  absl::StatusOr<bool> RenderPolygonDetails(ColliderPanelModel& model,
                                            std::size_t polygon_index);
  void SyncCanvas(ColliderPanelModel& model);

  std::unique_ptr<CanvasCollider> canvas_collider_;
  std::uint64_t canvas_revision_ = std::numeric_limits<std::uint64_t>::max();
  GuiInterface* gui_;
};

}  // namespace zebes
