#include "editor/level_editor/level_authoring_readiness.h"

#include <cmath>

#include "absl/status/status.h"

namespace zebes {

LevelAuthoringReadiness EvaluateLevelAuthoringReadiness(const Level& level, bool tileset_resolves,
                                                        bool active_world_layer_available,
                                                        bool parallax_theme_available,
                                                        bool zone_theme_references_resolve) {
  LevelAuthoringReadiness result;
  if (level.name.empty()) result.save_blockers.emplace_back("Enter a level name.");
  if (!std::isfinite(level.width) || !std::isfinite(level.height) || level.width <= 0.0 ||
      level.height <= 0.0) {
    result.save_blockers.emplace_back("World width and height must be positive.");
  } else if (level.tile_render_width > 0 && level.tile_render_height > 0 &&
             (std::fmod(level.width, level.tile_render_width) != 0.0 ||
              std::fmod(level.height, level.tile_render_height) != 0.0)) {
    result.save_blockers.emplace_back("World dimensions must be multiples of the tile size.");
  }
  if (level.tile_render_width <= 0 || level.tile_render_height <= 0) {
    result.save_blockers.emplace_back("Tile width and height must be positive.");
  }
  if (!std::isfinite(level.spawn_point.x) || !std::isfinite(level.spawn_point.y) ||
      level.spawn_point.x < 0.0 || level.spawn_point.y < 0.0 || level.spawn_point.x > level.width ||
      level.spawn_point.y > level.height) {
    result.save_blockers.emplace_back("Spawn point must be inside the world bounds.");
  }
  if (!zone_theme_references_resolve) {
    result.save_blockers.emplace_back("Every parallax zone must reference an available theme.");
  }

  Level validation_candidate = level;
  if (validation_candidate.id.empty()) validation_candidate.id = "pending";
  const absl::Status intrinsic = ValidateLevel(validation_candidate);
  if (!intrinsic.ok() && result.save_blockers.empty()) {
    result.save_blockers.emplace_back(intrinsic.message());
  }

  result.placement_blockers = result.save_blockers;
  if (level.tileset_id.empty()) {
    result.placement_blockers.emplace_back("Choose a tileset before placing level content.");
  } else if (!tileset_resolves) {
    result.placement_blockers.emplace_back("The selected tileset is missing from the catalog.");
  }
  if (!active_world_layer_available) {
    result.placement_blockers.emplace_back(
        "Select an available world layer before placing content.");
  }

  result.parallax_zone_blockers = result.save_blockers;
  if (!parallax_theme_available) {
    result.parallax_zone_blockers.emplace_back(
        "Create or import a parallax theme before adding a parallax zone.");
  }
  return result;
}

}  // namespace zebes
