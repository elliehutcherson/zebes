#pragma once

#include <string>
#include <vector>

#include "objects/level.h"

namespace zebes {

struct LevelAuthoringReadiness {
  std::vector<std::string> save_blockers;
  std::vector<std::string> placement_blockers;
  std::vector<std::string> parallax_zone_blockers;

  bool can_save() const { return save_blockers.empty(); }
  bool can_place() const { return placement_blockers.empty(); }
  bool can_add_parallax_zone() const { return parallax_zone_blockers.empty(); }
};

// Categorizes authoring prerequisites without depending on ImGui or the API.
// Catalog facts are supplied by the editor from its current stable-ID
// snapshots; this function remains the single policy rendered by every panel.
LevelAuthoringReadiness EvaluateLevelAuthoringReadiness(const Level& level, bool tileset_resolves,
                                                        bool active_world_layer_available,
                                                        bool parallax_theme_available,
                                                        bool zone_theme_references_resolve);

}  // namespace zebes
