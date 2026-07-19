#pragma once

#include <span>

#include "absl/status/status.h"
#include "editor/canvas/canvas.h"
#include "editor/level_editor/viewport_scene.h"

namespace zebes {

// ImGui/SDL presentation adapter for platform-neutral viewport descriptions.
// This is the boundary at which opaque engine texture handles become native
// renderer resources.
class ViewportRenderer {
 public:
  explicit ViewportRenderer(Canvas& canvas) : canvas_(canvas) {}

  absl::Status RenderEntities(std::span<const EntityRenderItem> items) const;
  absl::Status RenderTiles(const TileRenderBatch& batch) const;
  absl::Status RenderParallax(const ParallaxRenderBatch& batch) const;
  void RenderZoneGizmos(std::span<const ZoneGizmoItem> items) const;

 private:
  Canvas& canvas_;
};

}  // namespace zebes
