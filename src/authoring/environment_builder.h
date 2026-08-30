#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/vec.h"

namespace zebes {

class Api;

inline constexpr int kEnvironmentBuildSpecSchemaVersion = 2;

struct EnvironmentElementSpec {
  std::string name;
  std::string artwork_recipe_name;
  Vec position;
  float scale = 1.0f;
};

struct EnvironmentParallaxLayerSpec {
  std::string name;
  Vec scroll_factor;
  Vec offset;
  Vec repeat_period;
  std::vector<EnvironmentElementSpec> elements;
};

struct EnvironmentThemeSpec {
  std::string name;
  std::vector<EnvironmentParallaxLayerSpec> layers;
};

struct EnvironmentZoneSpec {
  std::string name;
  std::string theme_name;
  Vec min_point;
  Vec max_point;
  Vec fade_length;
};

struct EnvironmentTerrainRectangle {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool solid = true;
};

struct EnvironmentEntitySpec {
  uint64_t id = 0;
  std::string layer_name;
  std::string blueprint_name;
  std::string state_key;
  bool active = true;
  Vec position;
  int sort_order = 0;
};

struct EnvironmentLevelSpec {
  std::string name;
  std::string tileset_name;
  std::string terrain_name;
  int tile_render_width = 0;
  int tile_render_height = 0;
  int columns = 0;
  int rows = 0;
  Vec spawn_point;
  std::vector<std::string> world_layers;
  std::string gameplay_layer;
  std::vector<EnvironmentZoneSpec> zones;
  std::vector<EnvironmentEntitySpec> entities;
  std::vector<EnvironmentTerrainRectangle> terrain_rectangles;
};

struct EnvironmentBuildSpec {
  EnvironmentThemeSpec theme;
  EnvironmentLevelSpec level;
};

struct EnvironmentBuildResult {
  std::string theme_id;
  std::string level_id;
};

absl::StatusOr<EnvironmentBuildSpec> EnvironmentBuildSpecFromJson(const nlohmann::json& json);
absl::StatusOr<EnvironmentBuildSpec> ReadEnvironmentBuildSpec(const std::filesystem::path& path);

// Resolves all resource references by unique name. Resource IDs are generated
// by their owning managers on first build and preserved by name on repeat
// builds; authoring specifications never contain catalog GUIDs.
absl::StatusOr<EnvironmentBuildResult> BuildEnvironment(Api& api, const EnvironmentBuildSpec& spec);

}  // namespace zebes
