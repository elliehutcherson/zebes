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

inline constexpr int kOldestTerrainRecipeSchemaVersion = 1;
inline constexpr int kTerrainRecipeSchemaVersion = 3;

// JSON conversion is explicit rather than reflection-based so renaming a C++
// member cannot silently change the on-disk format. Every supported version is
// parsed strictly: migration may translate old meaning, but a missing field is
// corruption rather than permission to substitute today's default.
nlohmann::json TerrainRecipeToJson(const TerrainRecipe& recipe);
absl::StatusOr<TerrainRecipe> TerrainRecipeFromJson(const nlohmann::json& json);

}  // namespace zebes
