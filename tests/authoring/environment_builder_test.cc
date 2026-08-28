#include "authoring/environment_builder.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "api/api.h"
#include "artwork/parallax_artwork_recipe.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "objects/blueprint.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

std::filesystem::path ProductionSpecPath() {
  return std::filesystem::path(ZEBES_TEST_ASSETS_DIR) / "authoring" / "environments" /
         "catacombs_processional.json";
}

absl::StatusOr<nlohmann::json> ProductionSpecJson() {
  std::ifstream stream(ProductionSpecPath());
  if (!stream.is_open()) return absl::NotFoundError("could not open production environment spec");
  nlohmann::json json;
  stream >> json;
  return json;
}

EnvironmentBuildSpec EntityBuildSpec() {
  return {
      .theme =
          {
              .name = "Test Theme",
              .layers = {{
                  .name = "Far",
                  .scroll_factor = {0.1, 0.1},
                  .repeat_period = {320.0, 320.0},
                  .elements = {{
                      .name = "Backdrop",
                      .artwork_recipe_name = "Backdrop Recipe",
                      .scale = 1.0f,
                  }},
              }},
          },
      .level =
          {
              .name = "Test Level",
              .tileset_name = "Test Tileset",
              .terrain_name = "Stone",
              .tile_render_width = 32,
              .tile_render_height = 32,
              .columns = 10,
              .rows = 10,
              .spawn_point = {32.0, 32.0},
              .world_layers = {"Back Decor", "Gameplay"},
              .gameplay_layer = "Gameplay",
              .zones = {{
                  .name = "Test Zone",
                  .theme_name = "Test Theme",
                  .max_point = {320.0, 320.0},
              }},
              .entities = {{
                  .id = 7,
                  .layer_name = "Back Decor",
                  .blueprint_name = "Crystal",
                  .state_name = "Lit",
                  .active = false,
                  .position = {64.0, 96.0},
                  .sort_order = -3,
              }},
          },
  };
}

class EnvironmentApiFake final : public Api {
 public:
  EnvironmentApiFake() {
    recipes_.push_back({.name = "Backdrop Recipe", .texture_id = "texture-id"});
    tilesets_.push_back({
        .id = "tileset-id",
        .name = "Test Tileset",
        .terrains = {{.id = 1, .name = "Stone"}},
    });
    blueprints_.push_back({
        .id = "blueprint-id",
        .name = "Crystal",
        .states = {{
            .name = "Lit",
            .collider_id = "collider-id",
            .sprite_id = "sprite-id",
        }},
    });
  }

  std::vector<ParallaxArtworkRecipe> GetAllParallaxArtworkRecipes() const override {
    return recipes_;
  }

  std::vector<ParallaxTheme> GetAllParallaxThemes() override { return themes_; }

  absl::StatusOr<std::string> CreateParallaxTheme(ParallaxTheme theme) override {
    theme.id = "theme-id";
    themes_.push_back(std::move(theme));
    return themes_.back().id;
  }

  absl::Status UpdateParallaxTheme(ParallaxTheme theme) override {
    auto existing =
        std::find_if(themes_.begin(), themes_.end(),
                     [&theme](const ParallaxTheme& item) { return item.id == theme.id; });
    if (existing == themes_.end()) return absl::NotFoundError("theme not found");
    *existing = std::move(theme);
    return absl::OkStatus();
  }

  std::vector<Tileset> GetAllTilesets() override { return tilesets_; }

  absl::StatusOr<Tileset*> GetTileset(const std::string& id) override {
    auto selected = std::find_if(tilesets_.begin(), tilesets_.end(),
                                 [&id](const Tileset& tileset) { return tileset.id == id; });
    if (selected == tilesets_.end()) return absl::NotFoundError("tileset not found");
    return &*selected;
  }

  std::vector<Blueprint> GetAllBlueprints() override { return blueprints_; }
  std::vector<Level> GetAllLevels() override { return levels_; }

  absl::StatusOr<std::string> CreateLevel(Level level) override {
    level.id = "level-id";
    levels_.push_back(std::move(level));
    return levels_.back().id;
  }

  absl::Status UpdateLevel(Level level) override {
    auto existing = std::find_if(levels_.begin(), levels_.end(),
                                 [&level](const Level& item) { return item.id == level.id; });
    if (existing == levels_.end()) return absl::NotFoundError("level not found");
    *existing = std::move(level);
    return absl::OkStatus();
  }

  void AddBlueprint(Blueprint blueprint) { blueprints_.push_back(std::move(blueprint)); }
  const std::vector<ParallaxTheme>& themes() const { return themes_; }
  const std::vector<Level>& levels() const { return levels_; }

 private:
  std::vector<ParallaxArtworkRecipe> recipes_;
  std::vector<ParallaxTheme> themes_;
  std::vector<Tileset> tilesets_;
  std::vector<Blueprint> blueprints_;
  std::vector<Level> levels_;
};

TEST(EnvironmentBuilderTest, LoadsTheProductionCatacombsWithoutCatalogIds) {
  ASSERT_OK_AND_ASSIGN(const EnvironmentBuildSpec spec,
                       ReadEnvironmentBuildSpec(ProductionSpecPath()));

  EXPECT_EQ(spec.theme.layers.size(), 3);
  EXPECT_EQ(spec.theme.layers[0].elements.size(), 1);
  ASSERT_EQ(spec.theme.layers[1].elements.size(), 4);
  EXPECT_EQ(spec.theme.layers[1].elements[0].artwork_recipe_name,
            "Catacombs Far Lower Foundations");
  EXPECT_EQ(spec.theme.layers[1].elements[1].artwork_recipe_name,
            "Catacombs Far Lower Foundations B");
  ASSERT_EQ(spec.theme.layers[2].elements.size(), 8);
  EXPECT_EQ(spec.theme.layers[2].elements[0].artwork_recipe_name, "Catacombs Near Lower Rubble");
  EXPECT_EQ(spec.theme.layers[2].elements[1].artwork_recipe_name, "Catacombs Near Lower Rubble B");
  EXPECT_EQ(spec.theme.layers[2].elements[2].artwork_recipe_name, "Catacombs Near Lower Rubble");
  EXPECT_EQ(spec.theme.layers[2].elements[3].artwork_recipe_name, "Catacombs Near Lower Rubble B");
  EXPECT_EQ(spec.level.columns, 512);
  EXPECT_EQ(spec.level.rows, 40);
  EXPECT_EQ(spec.level.tileset_name, "lucinda_cave");
  EXPECT_EQ(spec.level.terrain_name, "lucinda_cave");
  ASSERT_EQ(spec.level.entities.size(), 14);
  EXPECT_EQ(spec.level.entities[0].id, 1);
  EXPECT_EQ(spec.level.entities[0].blueprint_name, "Cave Crystal");
  EXPECT_EQ(spec.level.entities[0].layer_name, "Back Decor");
  EXPECT_EQ(spec.level.entities[1].id, 2);
  EXPECT_EQ(spec.level.entities[1].blueprint_name, "Catacombs Ossuary Reliquary");
  EXPECT_EQ(spec.level.entities[1].layer_name, "Back Decor");
  EXPECT_EQ(spec.level.entities[1].position.x, 640.0f);
  EXPECT_EQ(spec.level.entities[1].position.y, 864.0f);
  EXPECT_EQ(spec.level.entities[2].id, 3);
  EXPECT_EQ(spec.level.entities[2].blueprint_name, "Catacombs Funeral Brazier");
  EXPECT_EQ(spec.level.entities[2].layer_name, "Front Decor");
  EXPECT_EQ(spec.level.entities[2].position.x, 1472.0f);
  EXPECT_EQ(spec.level.entities[2].position.y, 832.0f);
  EXPECT_EQ(spec.level.entities[3].id, 4);
  EXPECT_EQ(spec.level.entities[3].blueprint_name, "Mouse Player Placeholder");
  EXPECT_EQ(spec.level.entities[3].layer_name, "GamePlay");
  EXPECT_EQ(spec.level.entities[3].position.x, 256.0f);
  EXPECT_EQ(spec.level.entities[3].position.y, 864.0f);
  EXPECT_EQ(spec.level.entities[4].blueprint_name, "Catacombs Collapsed Offerings");
  EXPECT_EQ(spec.level.entities[5].blueprint_name, "Catacombs Fallen Votive Tablets");
  EXPECT_EQ(spec.level.entities[8].blueprint_name, "Catacombs Ceiling Chain Frieze");
  EXPECT_EQ(spec.level.entities[9].blueprint_name, "Catacombs Ceiling Chain Frieze");
  EXPECT_EQ(spec.level.entities[11].blueprint_name, "Catacombs Foreground Funeral Shroud");
  EXPECT_EQ(spec.level.entities[12].blueprint_name, "Catacombs Foreground Tattered Valance");
  EXPECT_EQ(spec.level.entities.back().position.x, 14272.0f);
}

TEST(EnvironmentBuilderTest, RejectsUnknownFieldsInsteadOfSilentlyIgnoringThem) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["theme"]["unrecognized"] = true;

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RejectsTerrainOutsideTheDeclaredGrid) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["level"]["terrain_rectangles"][0]["width"] = 513;

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RejectsDuplicateEntityIds) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["level"]["entities"].push_back(json["level"]["entities"].front());

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RejectsEntityOutsideTheDeclaredGrid) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["level"]["entities"][0]["position_pixels"] = {16385.0, 864.0};

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RejectsEntityInUnknownLayer) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["level"]["entities"][0]["layer_name"] = "Missing";

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(EnvironmentBuilderTest, RebuildsBlueprintEntitiesIdempotently) {
  EnvironmentApiFake api;
  const EnvironmentBuildSpec spec = EntityBuildSpec();

  ASSERT_OK(BuildEnvironment(api, spec));
  ASSERT_EQ(api.levels().size(), 1);
  const Level first = api.levels().front();
  ASSERT_OK(BuildEnvironment(api, spec));
  ASSERT_EQ(api.levels().size(), 1);
  EXPECT_EQ(api.levels().front(), first);

  const WorldLayer* back_decor = FindWorldLayer(api.levels().front(), 0);
  ASSERT_NE(back_decor, nullptr);
  ASSERT_EQ(back_decor->entities.size(), 1);
  const Entity& entity = back_decor->entities.at(7);
  EXPECT_FALSE(entity.active);
  EXPECT_EQ(entity.blueprint_id, "blueprint-id");
  EXPECT_EQ(entity.blueprint_state_index, 0);
  EXPECT_EQ(entity.sprite_id, "sprite-id");
  EXPECT_EQ(entity.collider_id, "collider-id");
  EXPECT_EQ(entity.transform.position, (Vec{64.0, 96.0}));
  EXPECT_EQ(entity.sort_order, -3);
}

TEST(EnvironmentBuilderTest, RefusesUnknownBlueprintBeforePublishingAnything) {
  EnvironmentApiFake api;
  EnvironmentBuildSpec spec = EntityBuildSpec();
  spec.level.entities.front().blueprint_name = "Missing";

  EXPECT_EQ(BuildEnvironment(api, spec).status().code(), absl::StatusCode::kNotFound);
  EXPECT_TRUE(api.themes().empty());
  EXPECT_TRUE(api.levels().empty());
}

TEST(EnvironmentBuilderTest, RefusesDuplicateBlueprintNames) {
  EnvironmentApiFake api;
  api.AddBlueprint({.id = "other-blueprint-id", .name = "Crystal"});

  EXPECT_EQ(BuildEnvironment(api, EntityBuildSpec()).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(api.themes().empty());
  EXPECT_TRUE(api.levels().empty());
}

TEST(EnvironmentBuilderTest, RefusesUnknownBlueprintState) {
  EnvironmentApiFake api;
  EnvironmentBuildSpec spec = EntityBuildSpec();
  spec.level.entities.front().state_name = "Missing";

  EXPECT_EQ(BuildEnvironment(api, spec).status().code(), absl::StatusCode::kNotFound);
  EXPECT_TRUE(api.themes().empty());
  EXPECT_TRUE(api.levels().empty());
}

TEST(EnvironmentBuilderTest, RejectsUnsupportedSchemaVersions) {
  ASSERT_OK_AND_ASSIGN(nlohmann::json json, ProductionSpecJson());
  json["schema_version"] = 1;

  EXPECT_EQ(EnvironmentBuildSpecFromJson(json).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
