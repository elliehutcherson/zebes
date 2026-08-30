#include "objects/level.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

Level ValidLevel() {
  return Level{
      .id = "level-id",
      .name = "Level",
      .width = 320,
      .height = 320,
  };
}

ParallaxZone Zone(int id, std::string name, std::string theme_id, Vec min_point, Vec max_point,
                  Vec fade_length = {}) {
  return {
      .id = id,
      .name = std::move(name),
      .theme_id = std::move(theme_id),
      .min_point = min_point,
      .max_point = max_point,
      .fade_length = fade_length,
  };
}

TEST(LevelTest, EntityIdsAreUniqueAcrossWorldLayers) {
  Level level = ValidLevel();
  level.layers.push_back(WorldLayer{.id = 1, .name = "Front"});
  ASSERT_OK(level.AddEntity(0, Entity{.id = 7}));

  EXPECT_EQ(level.AddEntity(1, Entity{.id = 7}).code(), absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(level.layers.front().entities.size(), 1u);
  EXPECT_TRUE(level.layers.back().entities.empty());
}

TEST(LevelTest, MoveEntityValidatesBeforeMutation) {
  Level level = ValidLevel();
  level.layers.push_back(WorldLayer{.id = 1, .name = "Front"});
  ASSERT_OK(level.AddEntity(0, Entity{.id = 7}));

  EXPECT_EQ(MoveEntityToLayer(level, 7, 99).code(), absl::StatusCode::kNotFound);
  EXPECT_NE(FindEntityLayer(level, 7), nullptr);
  EXPECT_EQ(FindEntityLayer(level, 7)->id, 0);

  ASSERT_OK(MoveEntityToLayer(level, 7, 1));
  EXPECT_EQ(FindEntityLayer(level, 7)->id, 1);
  EXPECT_EQ(FindEntity(level, 7)->id, 7);
}

TEST(LevelTest, ValidationRejectsLayerlessAndDuplicateLayerIds) {
  Level level = ValidLevel();
  level.layers.clear();
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);

  level.layers = {WorldLayer{.id = 2, .name = "Back"}, WorldLayer{.id = 2, .name = "Front"}};
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);
}

TEST(LevelTest, ValidationRejectsDuplicateEntitiesAcrossLayers) {
  Level level = ValidLevel();
  level.layers.push_back(WorldLayer{.id = 1, .name = "Front"});
  level.layers[0].entities.emplace(3, Entity{.id = 3});
  level.layers[1].entities.emplace(3, Entity{.id = 3});

  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);
}

TEST(LevelTest, ValidationRejectsInvalidChunkCoordinatesAndTilesOutsideBounds) {
  Level level = ValidLevel();
  level.layers.front().tile_chunks[ChunkKey(-1, 0)].tiles[0] = 1;
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);

  level.layers.front().tile_chunks.clear();
  level.layers.front().tile_chunks[ChunkKey(0, 0)].tiles[20] = 1;
  level.width = 16;
  level.height = 16;
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);
}

TEST(LevelTest, TileChunkCoordinatesUseRowMajorOrder) {
  EXPECT_LT((TileChunkCoordinate{.x = 99, .y = 1}), (TileChunkCoordinate{.x = -99, .y = 2}));
  EXPECT_LT((TileChunkCoordinate{.x = 1, .y = 3}), (TileChunkCoordinate{.x = 2, .y = 3}));
  EXPECT_EQ((TileChunkCoordinate{.x = 4, .y = 5}), (TileChunkCoordinate{.x = 4, .y = 5}));
}

TEST(LevelTest, GetTileAtReturnsZeroForMissingChunks) {
  const WorldLayer layer{.id = 0, .name = "Base"};

  ASSERT_OK_AND_ASSIGN(const int tile_id, GetTileAt(layer, 0, 0));
  EXPECT_EQ(tile_id, 0);
}

TEST(LevelTest, GetTileAtResolvesChunkAndCellBoundaries) {
  WorldLayer layer{.id = 0, .name = "Base"};
  layer.tile_chunks[ChunkKey(0, 0)].tiles[31 * TileChunk::kSize + 31] = 7;
  layer.tile_chunks[ChunkKey(1, 1)].tiles[0] = 9;

  ASSERT_OK_AND_ASSIGN(const int edge_tile, GetTileAt(layer, 31, 31));
  EXPECT_EQ(edge_tile, 7);
  ASSERT_OK_AND_ASSIGN(const int next_chunk_tile, GetTileAt(layer, 32, 32));
  EXPECT_EQ(next_chunk_tile, 9);
  ASSERT_OK_AND_ASSIGN(const int empty_cell, GetTileAt(layer, 32, 31));
  EXPECT_EQ(empty_cell, 0);
}

TEST(LevelTest, GetTileAtRejectsNegativeCoordinates) {
  const WorldLayer layer{.id = 0, .name = "Base"};

  EXPECT_EQ(GetTileAt(layer, -1, 0).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(GetTileAt(layer, 0, -1).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LevelTest, ValidationRequiresBlueprintIdentityAndStateKeyTogether) {
  Level level = ValidLevel();
  ASSERT_OK(level.AddEntity(0, Entity{.id = 1, .blueprint_id = "player"}));
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);

  Entity* entity = FindEntity(level, 1);
  ASSERT_NE(entity, nullptr);
  entity->blueprint_id.clear();
  entity->blueprint_state_key = "idle";
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);

  entity->blueprint_id = "player";
  EXPECT_OK(ValidateLevel(level));
}

TEST(LevelTest, ValidationRejectsMalformedZoneThemeIds) {
  Level level = ValidLevel();
  level.zones.push_back({
      .id = 1,
      .name = "Cave",
      .theme_id = "../outside",
      .min_point = {0, 0},
      .max_point = {320, 320},
  });

  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);
}

TEST(LevelTest, ValidationRejectsInvalidZoneFades) {
  Level level = ValidLevel();
  level.zones.push_back({
      .id = 1,
      .name = "Cave",
      .theme_id = "theme-1",
      .min_point = {0, 0},
      .max_point = {100, 80},
  });

  level.zones[0].fade_length = {-1, 0};
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);

  level.zones[0].fade_length = {51, 0};
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);

  level.zones[0].fade_length = {50, 40};
  EXPECT_OK(ValidateLevel(level));

  level.zones[0].fade_length.x = std::numeric_limits<double>::infinity();
  EXPECT_EQ(ValidateLevel(level).code(), absl::StatusCode::kInvalidArgument);
}

TEST(LevelTest, ParallaxEnvironmentUsesHalfOpenBoundsAndLaterOverlapPriority) {
  std::vector<ParallaxZone> zones{
      Zone(1, "Left", "theme-left", {0, 0}, {100, 200}),
      Zone(2, "Right", "theme-right", {100, 0}, {200, 200}),
      Zone(3, "Overlay", "theme-overlay", {50, 50}, {150, 150}),
  };

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> missing,
                       ResolveParallaxEnvironment(zones, {250, 50}));
  EXPECT_FALSE(missing.has_value());

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> boundary,
                       ResolveParallaxEnvironment(zones, {100, 25}));
  ASSERT_TRUE(boundary.has_value());
  EXPECT_EQ(boundary->active_zone_id, 2);
  EXPECT_EQ(boundary->primary.theme_id, "theme-right");

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> overlap,
                       ResolveParallaxEnvironment(zones, {100, 100}));
  ASSERT_TRUE(overlap.has_value());
  EXPECT_EQ(overlap->active_zone_id, 3);
  EXPECT_EQ(overlap->primary.theme_id, "theme-overlay");
}

TEST(LevelTest, VerticalFadeUsesOneContinuousSpanWithUnequalWidths) {
  const std::vector<ParallaxZone> zones{
      Zone(1, "Left", "theme-left", {0, 0}, {100, 100}, {20, 0}),
      Zone(2, "Right", "theme-right", {100, 0}, {200, 100}, {10, 0}),
  };

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> left,
                       ResolveParallaxEnvironment(zones, {90, 50}));
  ASSERT_TRUE(left.has_value());
  ASSERT_TRUE(left->secondary.has_value());
  EXPECT_EQ(left->active_zone_id, 1);
  EXPECT_EQ(left->primary.theme_id, "theme-left");
  EXPECT_EQ(left->secondary->theme_id, "theme-right");
  EXPECT_NEAR(left->secondary_weight, 1.0 / 3.0, 1e-12);

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> edge,
                       ResolveParallaxEnvironment(zones, {100, 50}));
  ASSERT_TRUE(edge.has_value());
  ASSERT_TRUE(edge->secondary.has_value());
  EXPECT_EQ(edge->active_zone_id, 2);
  EXPECT_NEAR(edge->secondary_weight, 2.0 / 3.0, 1e-12);

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> right,
                       ResolveParallaxEnvironment(zones, {105, 50}));
  ASSERT_TRUE(right.has_value());
  ASSERT_TRUE(right->secondary.has_value());
  EXPECT_EQ(right->active_zone_id, 2);
  EXPECT_NEAR(right->secondary_weight, 5.0 / 6.0, 1e-12);
}

TEST(LevelTest, HorizontalFadeUsesAuthoredYWidths) {
  const std::vector<ParallaxZone> zones{
      Zone(1, "Top", "theme-top", {0, 0}, {100, 100}, {0, 30}),
      Zone(2, "Bottom", "theme-bottom", {0, 100}, {100, 200}, {0, 10}),
  };

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> environment,
                       ResolveParallaxEnvironment(zones, {50, 90}));

  ASSERT_TRUE(environment.has_value());
  ASSERT_TRUE(environment->secondary.has_value());
  EXPECT_EQ(environment->primary.theme_id, "theme-top");
  EXPECT_EQ(environment->secondary->theme_id, "theme-bottom");
  EXPECT_DOUBLE_EQ(environment->secondary_weight, 0.5);
}

TEST(LevelTest, OneSidedFadesRemainContinuousAtTheHalfOpenBoundary) {
  std::vector<ParallaxZone> zones{
      Zone(1, "Left", "theme-left", {0, 0}, {100, 100}),
      Zone(2, "Right", "theme-right", {100, 0}, {200, 100}, {20, 0}),
  };

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> secondary_only_edge,
                       ResolveParallaxEnvironment(zones, {100, 50}));
  ASSERT_TRUE(secondary_only_edge.has_value());
  EXPECT_EQ(secondary_only_edge->active_zone_id, 2);
  EXPECT_EQ(secondary_only_edge->primary.theme_id, "theme-left");
  ASSERT_TRUE(secondary_only_edge->secondary.has_value());
  EXPECT_EQ(secondary_only_edge->secondary->theme_id, "theme-right");
  EXPECT_DOUBLE_EQ(secondary_only_edge->secondary_weight, 0.0);

  zones[0].fade_length.x = 20;
  zones[1].fade_length.x = 0;
  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> before_edge,
                       ResolveParallaxEnvironment(zones, {99, 50}));
  ASSERT_TRUE(before_edge.has_value());
  ASSERT_TRUE(before_edge->secondary.has_value());
  EXPECT_NEAR(before_edge->secondary_weight, 0.95, 1e-12);

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> primary_only_edge,
                       ResolveParallaxEnvironment(zones, {100, 50}));
  ASSERT_TRUE(primary_only_edge.has_value());
  EXPECT_EQ(primary_only_edge->active_zone_id, 2);
  EXPECT_EQ(primary_only_edge->primary.theme_id, "theme-right");
  EXPECT_FALSE(primary_only_edge->secondary.has_value());
}

TEST(LevelTest, ZeroFadesAndMissingNeighborsRemainUnblended) {
  const std::vector<ParallaxZone> adjacent{
      Zone(1, "Left", "theme-left", {0, 0}, {100, 100}),
      Zone(2, "Right", "theme-right", {100, 0}, {200, 100}),
  };
  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> hard_cut,
                       ResolveParallaxEnvironment(adjacent, {100, 50}));
  ASSERT_TRUE(hard_cut.has_value());
  EXPECT_EQ(hard_cut->primary.theme_id, "theme-right");
  EXPECT_FALSE(hard_cut->secondary.has_value());

  const std::vector<ParallaxZone> isolated{
      Zone(1, "Isolated", "theme-alone", {0, 0}, {100, 100}, {20, 20}),
  };
  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> missing_neighbor,
                       ResolveParallaxEnvironment(isolated, {95, 95}));
  ASSERT_TRUE(missing_neighbor.has_value());
  EXPECT_EQ(missing_neighbor->primary.theme_id, "theme-alone");
  EXPECT_FALSE(missing_neighbor->secondary.has_value());
}

TEST(LevelTest, PartialSharedEdgeUsesHalfOpenProjectionMembership) {
  std::vector<ParallaxZone> zones{
      Zone(1, "Left", "theme-left", {0, 0}, {100, 200}, {20, 0}),
      Zone(2, "Upper Right", "theme-upper", {100, 0}, {200, 100}, {20, 0}),
  };
  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> beyond_projection,
                       ResolveParallaxEnvironment(zones, {90, 150}));
  ASSERT_TRUE(beyond_projection.has_value());
  EXPECT_EQ(beyond_projection->primary.zone_id, 1);
  EXPECT_FALSE(beyond_projection->secondary.has_value());

  zones.push_back(Zone(3, "Lower Right", "theme-lower", {100, 100}, {200, 200}, {20, 0}));
  Level level = ValidLevel();
  level.zones = zones;
  ASSERT_OK(ValidateLevel(level));

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> environment,
                       ResolveParallaxEnvironment(zones, {90, 100}));

  ASSERT_TRUE(environment.has_value());
  ASSERT_TRUE(environment->secondary.has_value());
  EXPECT_EQ(environment->secondary->zone_id, 3);
  EXPECT_DOUBLE_EQ(environment->secondary_weight, 0.25);
}

TEST(LevelTest, SameThemeFadeCanonicalizesToOneTheme) {
  const std::vector<ParallaxZone> zones{
      Zone(1, "Left", "shared-theme", {0, 0}, {100, 100}, {20, 0}),
      Zone(2, "Right", "shared-theme", {100, 0}, {200, 100}, {20, 0}),
  };

  ASSERT_OK_AND_ASSIGN(const std::optional<ResolvedParallaxEnvironment> environment,
                       ResolveParallaxEnvironment(zones, {100, 50}));

  ASSERT_TRUE(environment.has_value());
  EXPECT_EQ(environment->active_zone_id, 2);
  EXPECT_EQ(environment->primary.zone_id, 1);
  EXPECT_FALSE(environment->secondary.has_value());
  EXPECT_DOUBLE_EQ(environment->secondary_weight, 0.0);
}

TEST(LevelTest, ValidationRejectsIntersectingFadeBandsWithZoneNames) {
  Level level = ValidLevel();
  level.zones = {
      Zone(1, "Top Left", "theme-tl", {0, 0}, {100, 100}, {20, 20}),
      Zone(2, "Top Right", "theme-tr", {100, 0}, {200, 100}, {20, 20}),
      Zone(3, "Bottom Left", "theme-bl", {0, 100}, {100, 200}, {20, 20}),
      Zone(4, "Bottom Right", "theme-br", {100, 100}, {200, 200}, {20, 20}),
  };

  const absl::Status status = ValidateLevel(level);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(absl::StrContains(status.message(), "Top Left"));
  EXPECT_TRUE(absl::StrContains(status.message(), "Top Right"));
}

TEST(LevelTest, ValidationRejectsFadeBandThroughThirdZoneOverlap) {
  Level level = ValidLevel();
  level.zones = {
      Zone(1, "Left", "theme-left", {0, 0}, {100, 200}, {20, 0}),
      Zone(2, "Right", "theme-right", {100, 0}, {200, 200}, {20, 0}),
      Zone(3, "Overlay", "theme-overlay", {90, 50}, {110, 150}),
  };

  const absl::Status status = ValidateLevel(level);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(absl::StrContains(status.message(), "Left"));
  EXPECT_TRUE(absl::StrContains(status.message(), "Right"));
  EXPECT_TRUE(absl::StrContains(status.message(), "Overlay"));
}

TEST(LevelTest, ResolverDefensivelyRejectsMultipleSeams) {
  const std::vector<ParallaxZone> zones{
      Zone(1, "Top Left", "theme-tl", {0, 0}, {100, 100}, {20, 20}),
      Zone(2, "Top Right", "theme-tr", {100, 0}, {200, 100}, {20, 20}),
      Zone(3, "Bottom Left", "theme-bl", {0, 100}, {100, 200}, {20, 20}),
      Zone(4, "Bottom Right", "theme-br", {100, 100}, {200, 200}, {20, 20}),
  };

  EXPECT_EQ(ResolveParallaxEnvironment(zones, {90, 90}).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace zebes
