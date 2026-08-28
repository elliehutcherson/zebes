#pragma once

#include <filesystem>
#include <string>

#include "absl/status/statusor.h"
#include "nlohmann/json_fwd.hpp"
#include "terrain/terrain_style.h"

namespace zebes {

class Api;

inline constexpr int kTerrainBuildSpecSchemaVersion = 1;

struct TerrainBuildSpec {
  std::string name;
  TerrainGenConfig config;
};

struct TerrainBuildResult {
  std::string recipe_id;
  std::string tileset_id;
  std::string texture_id;
  int tile_count = 0;
  bool created = false;
};

absl::StatusOr<TerrainBuildSpec> TerrainBuildSpecFromJson(const nlohmann::json& json);
absl::StatusOr<TerrainBuildSpec> ReadTerrainBuildSpec(const std::filesystem::path& path);

// Creates a new generated-terrain bundle when the spec name is absent, or
// regenerates the uniquely named existing bundle while preserving every ID.
// Topology changes remain forbidden by the production regeneration boundary.
absl::StatusOr<TerrainBuildResult> BuildTerrain(Api& api, const TerrainBuildSpec& spec);

}  // namespace zebes
