#include "editor/level_editor/viewport_interaction.h"

#include <limits>
#include <utility>

#include "absl/status/statusor.h"
#include "editor/level_editor/viewport_model.h"
#include "gtest/gtest.h"
#include "terrain/terrain_mask.h"
#include "macros.h"

namespace zebes {
namespace {

Level MakeLevel() {
  return Level{
      .tile_render_width = 16,
      .tile_render_height = 16,
      .width = 160,
      .height = 160,
  };
}

TEST(ViewportInteractionTileTest, PaintsAndErasesFromTranslatedPointerState) {
  ViewportInteractionController controller;
  Level level = MakeLevel();

  ASSERT_OK(controller.Update(
      level, {.world_position = {8, 8}, .pointer_in_level = true, .primary_down = true},
      {.paint_tile_id = 7}));
  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 7);

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {8, 8},
                           .pointer_in_level = true,
                           .secondary_pressed = true,
                           .secondary_down = true},
                          {.paint_tile_id = 7})
                  .ok());
  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 0);
}

TEST(ViewportInteractionTileTest, PrimaryPaintTakesPriorityWhenBothButtonsAreDown) {
  ViewportInteractionController controller;
  Level level = MakeLevel();

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {24, 8},
                           .pointer_in_level = true,
                           .primary_down = true,
                           .secondary_pressed = true,
                           .secondary_down = true},
                          {.paint_tile_id = 9})
                  .ok());

  EXPECT_EQ(GetTileAt(level, 1, 0).value(), 9);
}

TEST(ViewportInteractionTileTest, OutsidePointerIsANoOpAndInconsistentBoundsFail) {
  ViewportInteractionController controller;
  Level level = MakeLevel();

  EXPECT_OK(controller.Update(level, {.world_position = {-1, 8}, .primary_down = true},
                              {.paint_tile_id = 7}));
  EXPECT_TRUE(level.tile_chunks.empty());
  const double unavailable = -std::numeric_limits<float>::max();
  EXPECT_TRUE(controller
                  .Update(level,
                          {.world_position = {unavailable, unavailable}, .primary_down = true},
                          {.paint_tile_id = 7})
                  .ok());
  EXPECT_TRUE(level.tile_chunks.empty());
  EXPECT_EQ(controller
                .Update(level,
                        {.world_position = {-1, 8}, .pointer_in_level = true, .primary_down = true},
                        {.paint_tile_id = 7})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportInteractionEntityTest, PlacesInvisibleBlueprintWithoutASprite) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  Blueprint blueprint{
      .id = "marker",
      .states = {{.name = "Idle"}},
  };

  absl::StatusOr<ViewportInteractionResult> result = controller.Update(
      level, {.world_position = {32, 48}, .pointer_in_level = true, .primary_pressed = true},
      {.placement_blueprint = &blueprint});

  ASSERT_OK(result);
  ASSERT_TRUE(result->placed_entity.has_value());
  EXPECT_TRUE(result->placed_entity->sprite_id.empty());
}

TEST(ViewportInteractionEntityTest, PlacesResolvedBlueprintWithStableId) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  level.entities.emplace(50, Entity{.id = 50});
  Blueprint blueprint{
      .id = "enemy",
      .states = {{.name = "Idle", .sprite_id = "enemy-sprite"}},
  };
  Sprite sprite{.id = "enemy-sprite"};

  absl::StatusOr<ViewportInteractionResult> result =
      controller.Update(level,
                        {.world_position = {32, 48},
                         .pointer_in_level = true,
                         .primary_pressed = true,
                         .primary_down = true},
                        {.placement_blueprint = &blueprint, .placement_sprite = &sprite});

  ASSERT_OK(result);
  ASSERT_TRUE(result->placed_entity.has_value());
  EXPECT_EQ(result->placed_entity->id, 51);
  EXPECT_EQ(result->placed_entity->blueprint_id, "enemy");
  // The entity records the blueprint's authored sprite ID, not a pointer.
  EXPECT_EQ(result->placed_entity->sprite_id, "enemy-sprite");
  EXPECT_EQ(result->placed_entity->transform.position, (Vec{32, 48}));
}

TEST(ViewportInteractionEntityTest, RejectsUnresolvedBlueprintSprite) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  Blueprint blueprint{
      .id = "enemy",
      .states = {{.name = "Idle", .sprite_id = "missing"}},
  };

  EXPECT_EQ(
      controller
          .Update(level,
                  {.world_position = {32, 48}, .pointer_in_level = true, .primary_pressed = true},
                  {.placement_blueprint = &blueprint})
          .status()
          .code(),
      absl::StatusCode::kFailedPrecondition);
}

TEST(ViewportInteractionEntityTest, UsesFinalEntityIdThenFailsWhenExhausted) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  const uint64_t final_id = std::numeric_limits<uint64_t>::max();
  level.entities.emplace(final_id - 1, Entity{.id = final_id - 1});
  Blueprint blueprint{.id = "marker"};
  const ViewportInteractionInput input{
      .world_position = {32, 48},
      .pointer_in_level = true,
      .primary_pressed = true,
  };

  absl::StatusOr<ViewportInteractionResult> final_placement =
      controller.Update(level, input, {.placement_blueprint = &blueprint});
  ASSERT_OK(final_placement);
  ASSERT_TRUE(final_placement->placed_entity.has_value());
  EXPECT_EQ(final_placement->placed_entity->id, final_id);

  level.entities.emplace(final_id, std::move(*final_placement->placed_entity));
  EXPECT_EQ(controller.Update(level, input, {.placement_blueprint = &blueprint}).status().code(),
            absl::StatusCode::kResourceExhausted);
}

TEST(ViewportInteractionEntityTest, SelectsAndDragsWithStablePointerOffset) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  level.entities.emplace(4, Entity{.id = 4, .transform = {.position = {100, 100}}});

  absl::StatusOr<ViewportInteractionResult> pressed =
      controller.Update(level,
                        {.world_position = {104, 105},
                         .pointer_in_level = true,
                         .primary_pressed = true,
                         .primary_down = true},
                        {.selected_entity_id = 4});
  ASSERT_OK(pressed);
  EXPECT_EQ(pressed->selected_entity_id, 4);

  ASSERT_OK(controller.Update(
      level, {.world_position = {130, 140}, .pointer_in_level = true, .primary_down = true},
      {.selected_entity_id = 4}));
  EXPECT_EQ(level.entities.at(4).transform.position, (Vec{126, 135}));

  ASSERT_OK(
      controller.Update(level, {.world_position = {130, 140}}, {.selected_entity_id = 4}));
  ASSERT_OK(controller.Update(
      level, {.world_position = {150, 150}, .pointer_in_level = true, .primary_down = true},
      {.selected_entity_id = 4}));
  EXPECT_EQ(level.entities.at(4).transform.position, (Vec{126, 135}));
}

TEST(ViewportInteractionEntityTest, RequestsDeletionWithoutMutatingTheLevel) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  level.entities.emplace(4, Entity{.id = 4, .transform = {.position = {100, 100}}});

  absl::StatusOr<ViewportInteractionResult> result = controller.Update(
      level, {.world_position = {100, 100}, .pointer_in_level = true, .secondary_pressed = true},
      {.selected_entity_id = 4, .delete_mode = true});

  ASSERT_OK(result);
  EXPECT_EQ(result->delete_entity_id, 4);
  EXPECT_TRUE(level.entities.contains(4));
}

TEST(ViewportInteractionEntityTest, ChangingAuthoringModeCancelsAnActiveDrag) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  level.entities.emplace(4, Entity{.id = 4, .transform = {.position = {100, 100}}});

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {100, 100},
                           .pointer_in_level = true,
                           .primary_pressed = true,
                           .primary_down = true},
                          {.selected_entity_id = 4})
                  .ok());
  ASSERT_OK(controller.Update(level, {}, {.paint_tile_id = 7}));
  ASSERT_OK(controller.Update(
      level, {.world_position = {130, 140}, .pointer_in_level = true, .primary_down = true},
      {.selected_entity_id = 4}));

  EXPECT_EQ(level.entities.at(4).transform.position, (Vec{100, 100}));
}

TEST(ViewportInteractionTest, RejectsConflictingPlacementModes) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  Blueprint blueprint{.id = "enemy"};

  EXPECT_EQ(controller.Update(level, {}, {.paint_tile_id = 7, .placement_blueprint = &blueprint})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

// --- Paint deduplication ------------------------------------------------------

TEST(ViewportInteractionTileTest, HoldingOverOneCellDoesNotRewriteItEveryFrame) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  const ViewportInteractionInput held{
      .world_position = {8, 8}, .pointer_in_level = true, .primary_down = true};

  ASSERT_OK(controller.Update(level, held, {.paint_tile_id = 7}));
  ASSERT_EQ(GetTileAt(level, 0, 0).value(), 7);

  // A second frame on the same cell is a no-op, so overwriting the cell behind
  // the controller's back stays overwritten.
  ASSERT_OK(SetTileAt(level, 0, 0, 99));
  ASSERT_OK(controller.Update(level, held, {.paint_tile_id = 7}));
  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 99);
}

TEST(ViewportInteractionTileTest, DraggingToANewCellPaintsAgain) {
  ViewportInteractionController controller;
  Level level = MakeLevel();

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {8, 8},
                           .pointer_in_level = true,
                           .primary_down = true},
                          {.paint_tile_id = 7})
                  .ok());
  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {24, 8},
                           .pointer_in_level = true,
                           .primary_down = true},
                          {.paint_tile_id = 7})
                  .ok());

  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 7);
  EXPECT_EQ(GetTileAt(level, 1, 0).value(), 7);
}

TEST(ViewportInteractionTileTest, ReleasingTheButtonAllowsRepaintingTheSameCell) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  const ViewportInteractionInput held{
      .world_position = {8, 8}, .pointer_in_level = true, .primary_down = true};

  ASSERT_OK(controller.Update(level, held, {.paint_tile_id = 7}));
  // A frame with nothing held ends the stroke.
  ASSERT_OK(controller.Update(level, {.world_position = {8, 8}, .pointer_in_level = true},
                              {.paint_tile_id = 7}));

  ASSERT_OK(SetTileAt(level, 0, 0, 0));
  ASSERT_OK(controller.Update(level, held, {.paint_tile_id = 7}));
  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 7);
}

// --- Terrain mode -------------------------------------------------------------

namespace {

constexpr int kTerrainId = 2;

// A terrain covering every mask, with tile IDs offset so they are recognisable.
Tileset MakeTerrainTileset() {
  Tileset tileset;
  tileset.name = "Generated";
  tileset.texture_id = "tx";
  tileset.tile_width = 16;
  tileset.tile_height = 16;

  Terrain terrain;
  terrain.id = kTerrainId;
  terrain.name = "Grass";
  terrain.solid_outside_level = false;

  absl::Span<const uint8_t> masks = Blob47MaskTable();
  for (int i = 0; i < kBlob47TileCount; ++i) {
    const int tile_id = 100 + i;
    // Full blocks, as BuildTerrainCandidate emits them: the brush reads a
    // cell's geometry back off its tile when refreshing neighbours.
    tileset.tiles.push_back(Tile{.id = tile_id, .name = "T", .shape = TileShape::kFullBlock});
    terrain.rules.push_back(
        TerrainRule{.mask = masks[i], .variants = {TerrainVariant{.tile_id = tile_id}}});
  }
  tileset.terrains.push_back(std::move(terrain));
  return tileset;
}

}  // namespace

TEST(ViewportInteractionTerrainTest, PaintsAndErasesThroughTheBrush) {
  Tileset tileset = MakeTerrainTileset();
  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_OK(index);
  Blob47TileProvider provider(*index);

  ViewportInteractionController controller;
  Level level = MakeLevel();

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {8, 8},
                           .pointer_in_level = true,
                           .primary_down = true},
                          {.paint_terrain_id = kTerrainId, .terrain_index = &*index, .terrain_provider = &provider})
                  .ok());
  // Isolated cell resolves to mask 0, which is table index 0.
  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 100);

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {8, 8},
                           .pointer_in_level = true,
                           .secondary_pressed = true,
                           .secondary_down = true},
                          {.paint_terrain_id = kTerrainId, .terrain_index = &*index, .terrain_provider = &provider})
                  .ok());
  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 0);
}

TEST(ViewportInteractionTerrainTest, PaintingANeighbourReresolvesTheExistingCell) {
  Tileset tileset = MakeTerrainTileset();
  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_OK(index);
  Blob47TileProvider provider(*index);

  ViewportInteractionController controller;
  Level level = MakeLevel();
  const ViewportInteractionOptions options{
      .paint_terrain_id = kTerrainId, .terrain_index = &*index, .terrain_provider = &provider};

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {8, 8},
                           .pointer_in_level = true,
                           .primary_down = true},
                          options)
                  .ok());
  const int isolated = GetTileAt(level, 0, 0).value();

  ASSERT_TRUE(controller
                  .Update(level,
                          {.world_position = {24, 8},
                           .pointer_in_level = true,
                           .primary_down = true},
                          options)
                  .ok());

  EXPECT_NE(GetTileAt(level, 0, 0).value(), isolated)
      << "the first cell should have gained an eastern edge";
}

TEST(ViewportInteractionTerrainTest, RequiresATerrainIndex) {
  ViewportInteractionController controller;
  Level level = MakeLevel();

  EXPECT_EQ(controller
                .Update(level,
                        {.world_position = {8, 8},
                         .pointer_in_level = true,
                         .primary_down = true},
                        {.paint_terrain_id = kTerrainId})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportInteractionTerrainTest, RejectsCombiningTerrainWithTileMode) {
  ViewportInteractionController controller;
  Level level = MakeLevel();

  EXPECT_EQ(
      controller.Update(level, {}, {.paint_terrain_id = kTerrainId, .paint_tile_id = 7})
          .status()
          .code(),
      absl::StatusCode::kInvalidArgument);
}

TEST(ViewportInteractionTerrainTest, RejectsCombiningTerrainWithBlueprintMode) {
  ViewportInteractionController controller;
  Level level = MakeLevel();
  Blueprint blueprint{.id = "enemy"};

  EXPECT_EQ(controller
                .Update(level, {},
                        {.paint_terrain_id = kTerrainId, .placement_blueprint = &blueprint})
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
