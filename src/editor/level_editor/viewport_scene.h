#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "engine/texture_handle.h"
#include "editor/level_editor/viewport_model.h"
#include "objects/camera.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/vec.h"

namespace zebes {

struct PixelRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr bool IsValid() const { return x >= 0 && y >= 0 && width > 0 && height > 0; }
};

struct SpriteRenderItem {
  TextureHandle texture;
  PixelRect source;
};

// Platform-neutral description of one entity in the editor viewport. Styling
// and native texture conversion remain the renderer's responsibility.
struct EntityRenderItem {
  uint64_t entity_id = Entity::kInvalidId;
  WorldRect bounds;
  std::optional<SpriteRenderItem> sprite;
  float overlay_opacity = 0.0f;
  bool show_border = false;
  bool selected = false;
};

struct EntityRenderOptions {
  uint64_t selected_entity_id = Entity::kInvalidId;
  bool show_borders = false;
  float overlay_opacity = 0.0f;
};

enum class ZoneGizmoState {
  kNormal,
  kActive,
  kSelected,
};

struct ZoneGizmoItem {
  int zone_id = -1;
  WorldRect bounds;
  ZoneGizmoState state = ZoneGizmoState::kNormal;
};

// Builds render items in stable entity-ID order. Inactive entities are omitted.
// Invalid sprite geometry and opacity are rejected instead of being rendered
// with undefined bounds or color values.
absl::StatusOr<std::vector<EntityRenderItem>> ComposeEntityRenderItems(
    const std::map<uint64_t, Entity>& entities, const EntityRenderOptions& options);

// Builds gizmos for zones intersecting the current camera. Selection takes
// visual precedence over active-zone state.
absl::StatusOr<std::vector<ZoneGizmoItem>> ComposeZoneGizmoItems(
    const std::vector<ParallaxZone>& zones, const Camera& camera,
    std::optional<int> selected_zone_id, std::optional<int> active_zone_id);

}  // namespace zebes
