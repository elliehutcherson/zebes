#pragma once

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "nlohmann/json_fwd.hpp"
#include "terrain/terrain_style.h"

namespace zebes {

// Authoring metadata for a generated terrain. Runtime tilesets deliberately do
// not own this: a game only needs tile rules, while the editor needs the full
// set of inputs required to reproduce the artwork.
struct TerrainRecipe {
  std::string id;
  std::string name;
  std::string tileset_id;
  std::string texture_id;
  int terrain_id = 0;
  std::optional<std::string> source_preset;
  TerrainGenConfig config;
};

// Only the current version is read. Versions 1 and 2 were migrated in
// `scripts/migrate_definitions.py`, which is where an older document is brought
// forward; carrying a translation for a version no file uses would mean the
// parser's shape was decided by data that no longer exists.
inline constexpr int kOldestTerrainRecipeSchemaVersion = 3;
inline constexpr int kTerrainRecipeSchemaVersion = 3;

// The config conversion is shared with deterministic terrain-build
// specifications. Keeping one parser prevents headless authoring and persisted
// recipes from assigning different meanings to the same generator settings.
nlohmann::json TerrainGenConfigToJson(const TerrainGenConfig& config);
absl::StatusOr<TerrainGenConfig> TerrainGenConfigFromJson(const nlohmann::json& json);

// JSON conversion is explicit rather than reflection-based so renaming a C++
// member cannot silently change the on-disk format. Parsing is strict: a
// missing field is corruption rather than permission to substitute today's
// default.
nlohmann::json TerrainRecipeToJson(const TerrainRecipe& recipe);
absl::StatusOr<TerrainRecipe> TerrainRecipeFromJson(const nlohmann::json& json);

}  // namespace zebes
