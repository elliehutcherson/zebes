#include "engine/tile_movement.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/level.h"
#include "objects/tileset.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr int kTileSize = 32;

void PutTile(WorldLayer& layer, int tile_x, int tile_y, int tile_id) {
  const int chunk_x = tile_x / TileChunk::kSize;
  const int chunk_y = tile_y / TileChunk::kSize;
  const int local_x = tile_x % TileChunk::kSize;
  const int local_y = tile_y % TileChunk::kSize;
  layer.tile_chunks[ChunkKey(chunk_x, chunk_y)].tiles[local_y * TileChunk::kSize + local_x] =
      tile_id;
}

TileMovementOptions Options(const WorldLayer& layer, const TileCollisionLookup& tiles,
                            AxisAlignedBox box, Vec displacement, Vec velocity = {}) {
  return {
      .layer = layer,
      .tiles = tiles,
      .tile_width = kTileSize,
      .tile_height = kTileSize,
      .box = box,
      .displacement = displacement,
      .velocity = velocity,
  };
}

TEST(TileMovementTest, CollisionLookupRejectsMalformedTilesets) {
  Tileset tileset{.tiles = {{.id = 1, .shape = TileShape::kFullBlock}}};
  ASSERT_OK_AND_ASSIGN(const TileCollisionLookup lookup, BuildTileCollisionLookup(tileset));
  EXPECT_EQ(lookup.at(1).shape, TileShape::kFullBlock);

  tileset.tiles.push_back({.id = 1, .shape = TileShape::kNone});
  EXPECT_TRUE(absl::IsInvalidArgument(BuildTileCollisionLookup(tileset).status()));
  tileset.tiles.back().id = 0;
  EXPECT_TRUE(absl::IsInvalidArgument(BuildTileCollisionLookup(tileset).status()));
}

TEST(TileMovementTest, StopsAtHighSpeedWallWithoutScanningDistantChunks) {
  WorldLayer layer;
  PutTile(layer, 2, 0, 1);
  PutTile(layer, 3200, 0, 999);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kFullBlock}}};

  ASSERT_OK_AND_ASSIGN(const TileMovementResult result,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {0, 8}, .max = {16, 24}}, {128, 0}, {128, 0})));

  EXPECT_DOUBLE_EQ(result.box.max.x, 64.0);
  EXPECT_DOUBLE_EQ(result.velocity.x, 0.0);
  ASSERT_EQ(result.contact_count, 1u);
  EXPECT_EQ(result.contacts[0].tile_x, 2);
}

TEST(TileMovementTest, CollectsBorderingFloorTilesAsOneGroundedManifold) {
  WorldLayer layer;
  PutTile(layer, 0, 1, 1);
  PutTile(layer, 1, 1, 1);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kFullBlock}}};

  ASSERT_OK_AND_ASSIGN(const TileMovementResult result,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {24, 0}, .max = {40, 16}}, {0, 32}, {0, 32})));

  EXPECT_DOUBLE_EQ(result.box.max.y, 32.0);
  EXPECT_DOUBLE_EQ(result.velocity.y, 0.0);
  EXPECT_TRUE(result.grounded);
  ASSERT_EQ(result.contact_count, 2u);
  EXPECT_EQ(result.contacts[0].tile_x, 0);
  EXPECT_EQ(result.contacts[1].tile_x, 1);
}

TEST(TileMovementTest, OneWayTileAllowsPassageAndBlocksFalling) {
  WorldLayer layer;
  PutTile(layer, 0, 1, 1);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kFullBlock, .is_one_way = true}}};

  ASSERT_OK_AND_ASSIGN(const TileMovementResult rising,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {8, 64}, .max = {24, 80}}, {0, -64}, {0, -64})));
  EXPECT_DOUBLE_EQ(rising.box.min.y, 0.0);
  EXPECT_EQ(rising.contact_count, 0u);

  ASSERT_OK_AND_ASSIGN(const TileMovementResult falling,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {8, 0}, .max = {24, 16}}, {0, 32}, {0, 32})));
  EXPECT_DOUBLE_EQ(falling.box.max.y, 32.0);
  EXPECT_TRUE(falling.grounded);
}

TEST(TileMovementTest, OneWayTileDoesNotRetroactivelyBlockAnInitialOverlap) {
  WorldLayer layer;
  PutTile(layer, 0, 1, 1);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kFullBlock, .is_one_way = true}}};

  ASSERT_OK_AND_ASSIGN(const TileMovementResult result,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {8, 40}, .max = {24, 56}}, {0, 8}, {0, 8})));

  EXPECT_EQ(result.box, (AxisAlignedBox{.min = {8, 48}, .max = {24, 64}}));
  EXPECT_EQ(result.contact_count, 0u);
  EXPECT_FALSE(result.grounded);
}

TEST(TileMovementTest, ProjectsVelocityAlongSlope) {
  WorldLayer layer;
  PutTile(layer, 0, 1, 1);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kSlope45FloorTallRight}}};

  ASSERT_OK_AND_ASSIGN(const TileMovementResult result,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {16, 0}, .max = {24, 8}}, {0, 48}, {0, 48})));

  ASSERT_GT(result.contact_count, 0u);
  EXPECT_LT(result.contacts[0].normal.y, 0.0);
  EXPECT_TRUE(result.grounded);
  EXPECT_LT(result.velocity.y, 48.0);
  EXPECT_LT(result.velocity.x, 0.0);
}

TEST(TileMovementTest, ResolvesCeilingAndSimultaneousCornerContacts) {
  WorldLayer ceiling_layer;
  PutTile(ceiling_layer, 0, 0, 1);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kFullBlock}}};
  ASSERT_OK_AND_ASSIGN(
      const TileMovementResult ceiling,
      MoveBoxThroughTileLayer(
          Options(ceiling_layer, tiles, {.min = {8, 64}, .max = {24, 80}}, {0, -64}, {0, -64})));
  EXPECT_DOUBLE_EQ(ceiling.box.min.y, 32.0);
  ASSERT_EQ(ceiling.contact_count, 1u);
  EXPECT_EQ(ceiling.contacts[0].normal, (Vec{0, 1}));

  WorldLayer corner_layer;
  PutTile(corner_layer, 2, 1, 1);
  PutTile(corner_layer, 1, 2, 1);
  ASSERT_OK_AND_ASSIGN(
      const TileMovementResult corner,
      MoveBoxThroughTileLayer(
          Options(corner_layer, tiles, {.min = {16, 16}, .max = {32, 32}}, {32, 32}, {32, 32})));
  EXPECT_EQ(corner.box.max, (Vec{64, 64}));
  ASSERT_EQ(corner.contact_count, 2u);
  EXPECT_EQ(corner.contacts[0].normal, (Vec{-1, 0}));
  EXPECT_EQ(corner.contacts[1].normal, (Vec{0, -1}));
  EXPECT_EQ(corner.velocity, (Vec{0, 0}));
}

TEST(TileMovementTest, CrossesGentleSlopeInternalSeamWithoutAnExposedWall) {
  WorldLayer layer;
  PutTile(layer, 0, 1, 1);
  PutTile(layer, 1, 1, 2);
  const TileCollisionLookup tiles{
      {1, {.shape = TileShape::kGentleSlopeFloorTallRightLower}},
      {2, {.shape = TileShape::kGentleSlopeFloorTallRightUpper}},
  };

  ASSERT_OK_AND_ASSIGN(const TileMovementResult result,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {4, 50}, .max = {12, 58}}, {40, -16}, {40, -16})));

  EXPECT_GT(result.box.min.x, 32.0);
  for (size_t index = 0; index < result.contact_count; ++index) {
    EXPECT_NE(result.contacts[index].normal, (Vec{-1, 0}));
  }
}

TEST(TileMovementTest, PartialNeighborDoesNotHideTheRestOfAFullBlockFace) {
  WorldLayer layer;
  PutTile(layer, 0, 0, 1);
  PutTile(layer, 1, 0, 2);
  const TileCollisionLookup tiles{
      {1, {.shape = TileShape::kHalfBlockBottom}},
      {2, {.shape = TileShape::kFullBlock}},
  };

  ASSERT_OK_AND_ASSIGN(const TileMovementResult result,
                       MoveBoxThroughTileLayer(Options(
                           layer, tiles, {.min = {8, 4}, .max = {16, 12}}, {32, 0}, {32, 0})));

  EXPECT_DOUBLE_EQ(result.box.max.x, 32.0);
  ASSERT_EQ(result.contact_count, 1u);
  EXPECT_EQ(result.contacts[0].normal, (Vec{-1, 0}));
}

TEST(TileMovementTest, ReportsUnknownLocalTileAndInitialOverlapWithoutMutation) {
  WorldLayer layer;
  PutTile(layer, 0, 0, 99);
  const TileCollisionLookup empty;
  const AxisAlignedBox box{.min = {0, 0}, .max = {16, 16}};
  EXPECT_TRUE(absl::IsFailedPrecondition(
      MoveBoxThroughTileLayer(Options(layer, empty, box, {1, 0})).status()));

  const TileCollisionLookup tiles{{99, {.shape = TileShape::kFullBlock}}};
  EXPECT_TRUE(absl::IsFailedPrecondition(
      MoveBoxThroughTileLayer(Options(layer, tiles, box, {1, 0})).status()));
}

TEST(TileMovementTest, ContactOrderDoesNotDependOnChunkInsertionOrder) {
  WorldLayer left_first;
  PutTile(left_first, 0, 1, 1);
  PutTile(left_first, 1, 1, 1);
  WorldLayer right_first;
  PutTile(right_first, 1, 1, 1);
  PutTile(right_first, 0, 1, 1);
  const TileCollisionLookup tiles{{1, {.shape = TileShape::kFullBlock}}};
  const AxisAlignedBox box{.min = {24, 0}, .max = {40, 16}};

  ASSERT_OK_AND_ASSIGN(const TileMovementResult left,
                       MoveBoxThroughTileLayer(Options(left_first, tiles, box, {0, 32})));
  ASSERT_OK_AND_ASSIGN(const TileMovementResult right,
                       MoveBoxThroughTileLayer(Options(right_first, tiles, box, {0, 32})));

  EXPECT_EQ(left.box, right.box);
  EXPECT_EQ(left.contact_count, right.contact_count);
  ASSERT_EQ(left.contact_count, 2u);
  EXPECT_EQ(left.contacts[0].tile_x, right.contacts[0].tile_x);
  EXPECT_EQ(left.contacts[1].tile_x, right.contacts[1].tile_x);
}

TEST(TileMovementTest, RejectsInvalidMovementGeometry) {
  const WorldLayer layer;
  const TileCollisionLookup tiles;
  EXPECT_TRUE(absl::IsInvalidArgument(
      MoveBoxThroughTileLayer(Options(layer, tiles, {.min = {0, 0}, .max = {0, 1}}, {1, 0}))
          .status()));
  TileMovementOptions invalid_dimensions =
      Options(layer, tiles, {.min = {0, 0}, .max = {1, 1}}, {1, 0});
  invalid_dimensions.tile_width = 0;
  EXPECT_TRUE(absl::IsInvalidArgument(MoveBoxThroughTileLayer(invalid_dimensions).status()));
}

}  // namespace
}  // namespace zebes
