#include "game/runtime_world.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "engine/input_types.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/blueprint.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/tileset.h"
#include "objects/transform.h"
#include "objects/vec.h"

namespace zebes {
namespace {

constexpr char kPlayerBlueprintId[] = "player-blueprint";
constexpr char kPlayerColliderId[] = "player-collider";

Collider PlayerCollider() {
  return {
      .id = kPlayerColliderId,
      .name = "MousePlayerBody32x64",
      .polygons = {{{16.0, -64.0}, {-16.0, -64.0}, {-16.0, 0.0}, {16.0, 0.0}}},
  };
}

Entity PlayerEntity(uint64_t id = 7) {
  return {
      .id = id,
      .active = true,
      .transform = {.position = {128.0, 192.0}},
      .blueprint_id = kPlayerBlueprintId,
      .blueprint_state_key = "default",
      .collider_id = kPlayerColliderId,
  };
}

Level TestLevel() {
  return {
      .id = "test-level",
      .name = "Test Level",
      .tileset_id = "test-tileset",
      .tile_render_width = 32,
      .tile_render_height = 32,
      .width = 1024.0,
      .height = 1024.0,
      .spawn_point = {0.0, 0.0},
  };
}

Tileset TestTileset() {
  return {
      .id = "test-tileset",
      .name = "Test Tileset",
      .tiles =
          {
              {.id = 1, .shape = TileShape::kFullBlock},
              {.id = 2, .shape = TileShape::kSlope45FloorTallRight},
          },
  };
}

void PutTile(WorldLayer& layer, int tile_x, int tile_y, int tile_id = 1) {
  const int chunk_x = tile_x / TileChunk::kSize;
  const int chunk_y = tile_y / TileChunk::kSize;
  const int local_x = tile_x % TileChunk::kSize;
  const int local_y = tile_y % TileChunk::kSize;
  layer.tile_chunks[ChunkKey(chunk_x, chunk_y)].tiles[local_y * TileChunk::kSize + local_x] =
      tile_id;
}

LoadedLevelContent WorldContent(Level level) {
  const Blueprint player_blueprint{
      .id = kPlayerBlueprintId,
      .name = "Player",
      .states = {{.key = "default", .name = "Default", .collider_id = kPlayerColliderId}},
  };
  return {
      .level = std::move(level),
      .tileset = TestTileset(),
      .blueprints = {{player_blueprint.id, player_blueprint}},
      .colliders = {{kPlayerColliderId, PlayerCollider()}},
  };
}

RuntimeWorld::Options WorldOptions() { return {.player_blueprint_id = kPlayerBlueprintId}; }

TEST(RuntimeWorldTest, OwnsRuntimeStateWithoutMutatingAuthoredLevel) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  Entity static_entity = PlayerEntity(8);
  static_entity.blueprint_id.clear();
  static_entity.blueprint_state_key.clear();
  static_entity.collider_id.clear();
  static_entity.body.is_static = true;
  ASSERT_OK(level.AddEntity(0, static_entity));
  const Level authored_level = level;

  LoadedLevelContent content = WorldContent(std::move(level));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));

  EXPECT_EQ(&world->level(), &content.level);
  EXPECT_EQ(world->level(), authored_level);
  EXPECT_EQ(world->player_entity_id(), 7);
  EXPECT_EQ(world->player_layer_id(), 0);
  EXPECT_EQ(world->player_local_collider(),
            (AxisAlignedBox{.min = {-16.0, -64.0}, .max = {16.0, 0.0}}));
  ASSERT_NE(world->FindTransform(7), nullptr);
  EXPECT_EQ(world->FindTransform(7)->position, (Vec{128.0, 192.0}));
  EXPECT_NE(world->FindMotion(7), nullptr);
  EXPECT_NE(world->FindPlayerController(7), nullptr);
  EXPECT_NE(world->FindTransform(8), nullptr);
  EXPECT_EQ(world->FindMotion(8), nullptr);
  EXPECT_EQ(world->FindTransform(999), nullptr);
  EXPECT_EQ(world->transforms().size(), 2u);

  world->FindTransform(7)->position.x = 400.0;
  EXPECT_DOUBLE_EQ(world->FindTransform(7)->position.x, 400.0);
  EXPECT_DOUBLE_EQ(FindEntity(world->level(), 7)->transform.position.x, 128.0);
}

TEST(RuntimeWorldTest, AdvancesAnimationAndResetsPlaybackWhenBlueprintStateChanges) {
  Level level = TestLevel();
  Entity player = PlayerEntity();
  player.blueprint_state_key = "idle";
  player.sprite_id = "idle";
  ASSERT_OK(level.AddEntity(0, player));
  const Level authored = level;
  LoadedLevelContent content = WorldContent(std::move(level));
  content.blueprints.at(kPlayerBlueprintId).states = {
      {.key = "idle", .name = "Idle", .collider_id = kPlayerColliderId, .sprite_id = "idle"},
      {.key = "moving", .name = "Moving", .collider_id = kPlayerColliderId, .sprite_id = "moving"},
  };
  content.sprites = {
      {"idle", Sprite{.id = "idle", .frames = {{.frames_per_cycle = 2}, {.frames_per_cycle = 2}}}},
      {"moving",
       Sprite{.id = "moving", .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
  };
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));

  ASSERT_NE(world->FindBlueprintStateIndex(7), nullptr);
  EXPECT_EQ(*world->FindBlueprintStateIndex(7), 0);
  EXPECT_EQ(world->sprite_ids().at(7), "idle");
  EXPECT_EQ(world->frame_indices().at(7), 0);

  world->AdvanceAnimations();
  EXPECT_EQ(world->frame_indices().at(7), 0);
  world->AdvanceAnimations();
  EXPECT_EQ(world->frame_indices().at(7), 1);

  ASSERT_OK_AND_ASSIGN(const RuntimeWorld::ResolvedBlueprintState moving,
                       world->ResolveEntityBlueprintState(7, "moving"));
  ASSERT_OK(world->SetEntityBlueprintState(7, moving));
  EXPECT_EQ(*world->FindBlueprintStateIndex(7), 1);
  EXPECT_EQ(world->sprite_ids().at(7), "moving");
  EXPECT_EQ(world->frame_indices().at(7), 0);
  world->AdvanceAnimations();
  EXPECT_EQ(world->frame_indices().at(7), 1);
  ASSERT_OK(world->SetEntityBlueprintState(7, moving));
  world->AdvanceAnimations();
  EXPECT_EQ(world->frame_indices().at(7), 0);

  EXPECT_EQ(world->level(), authored);
}

TEST(RuntimeWorldTest, BlueprintStateSelectionValidatesBeforeMutation) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  LoadedLevelContent content = WorldContent(std::move(level));
  content.blueprints.at(kPlayerBlueprintId)
      .states.push_back({.key = "different-collider",
                         .name = "Different Collider",
                         .collider_id = "other",
                         .sprite_id = "other"});
  content.sprites.emplace("other", Sprite{.id = "other", .frames = {{.frames_per_cycle = 1}}});
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));

  EXPECT_TRUE(absl::IsInvalidArgument(world->ResolveEntityBlueprintState(7, "missing").status()));
  EXPECT_TRUE(absl::IsFailedPrecondition(
      world->ResolveEntityBlueprintState(7, "different-collider").status()));
  EXPECT_TRUE(absl::IsNotFound(world->ResolveEntityBlueprintState(999, "default").status()));
  EXPECT_EQ(*world->FindBlueprintStateIndex(7), 0);
  EXPECT_EQ(world->sprite_ids().at(7), "");
  EXPECT_FALSE(world->frame_indices().contains(7));
}

TEST(RuntimeWorldTest, RejectsInvalidOrDuplicateBlueprintStateKeysAtBoot) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));

  LoadedLevelContent invalid = WorldContent(level);
  invalid.blueprints.at(kPlayerBlueprintId).states.front().key = "Invalid Key";
  EXPECT_EQ(RuntimeWorld::Create(invalid, WorldOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);

  LoadedLevelContent duplicate = WorldContent(std::move(level));
  duplicate.blueprints.at(kPlayerBlueprintId)
      .states.push_back({.key = "default", .name = "Duplicate", .collider_id = kPlayerColliderId});
  EXPECT_EQ(RuntimeWorld::Create(duplicate, WorldOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(RuntimeWorldTest, ConsumesJumpEdgeOnceAcrossCatchUpTicks) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  LoadedLevelContent content = WorldContent(std::move(level));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));
  InputSnapshot pressed;
  pressed.SetKeyDown(Key::kD);
  pressed.SetKeyDown(Key::kSpace);

  world->ApplyPlayerInput(pressed);
  ASSERT_NE(world->FindPlayerController(7), nullptr);
  EXPECT_EQ(world->FindPlayerController(7)->intent.horizontal_axis, 1);
  EXPECT_TRUE(world->FindPlayerController(7)->intent.jump_pressed);

  world->ApplyPlayerInput(pressed);
  EXPECT_FALSE(world->FindPlayerController(7)->intent.jump_pressed);
  EXPECT_TRUE(world->FindPlayerController(7)->intent.jump_held);

  world->ApplyPlayerInput({});
  world->ApplyPlayerInput(pressed);
  EXPECT_TRUE(world->FindPlayerController(7)->intent.jump_pressed);
}

TEST(RuntimeWorldTest, RejectsMissingAndDuplicatePlayers) {
  Level level = TestLevel();
  LoadedLevelContent missing = WorldContent(level);
  EXPECT_EQ(RuntimeWorld::Create(missing, WorldOptions()).status().code(),
            absl::StatusCode::kNotFound);

  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  level.layers.push_back({.id = 1, .name = "Other"});
  ASSERT_OK(level.AddEntity(1, PlayerEntity(8)));
  LoadedLevelContent duplicate = WorldContent(std::move(level));
  EXPECT_EQ(RuntimeWorld::Create(duplicate, WorldOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(RuntimeWorldTest, RejectsInvalidPlayerEntityState) {
  for (int variation = 0; variation < 5; ++variation) {
    Level level = TestLevel();
    Entity player = PlayerEntity();
    if (variation == 0) player.active = false;
    if (variation == 1) player.body.is_static = true;
    if (variation == 2) player.collider_id = "wrong-collider";
    if (variation == 3) player.transform.rotation = 1.0F;
    if (variation == 4) player.transform.position.x = std::nan("");
    ASSERT_OK(level.AddEntity(0, player));

    LoadedLevelContent content = WorldContent(std::move(level));
    const absl::Status status = RuntimeWorld::Create(content, WorldOptions()).status();
    const absl::StatusCode expected_code =
        variation == 4 ? absl::StatusCode::kInvalidArgument : absl::StatusCode::kFailedPrecondition;
    EXPECT_EQ(status.code(), expected_code) << "variation " << variation;
  }
}

TEST(RuntimeWorldTest, RejectsInvalidPlayerCollider) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));

  LoadedLevelContent empty_id = WorldContent(level);
  empty_id.colliders.at(kPlayerColliderId).id.clear();
  EXPECT_EQ(RuntimeWorld::Create(empty_id, WorldOptions()).status().code(),
            absl::StatusCode::kInvalidArgument);

  LoadedLevelContent wrong_size = WorldContent(level);
  wrong_size.colliders.at(kPlayerColliderId).polygons.front()[0].y = -63.0;
  wrong_size.colliders.at(kPlayerColliderId).polygons.front()[1].y = -63.0;
  EXPECT_EQ(RuntimeWorld::Create(wrong_size, WorldOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);

  LoadedLevelContent missing_polygon = WorldContent(level);
  missing_polygon.colliders.at(kPlayerColliderId).polygons.clear();
  EXPECT_EQ(RuntimeWorld::Create(missing_polygon, WorldOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);

  LoadedLevelContent non_finite = WorldContent(level);
  non_finite.colliders.at(kPlayerColliderId).polygons.front()[0].x = std::nan("");
  EXPECT_EQ(RuntimeWorld::Create(non_finite, WorldOptions()).status().code(),
            absl::StatusCode::kInvalidArgument);

  LoadedLevelContent non_rectangle = WorldContent(std::move(level));
  non_rectangle.colliders.at(kPlayerColliderId).polygons.front()[3] = {0.0, 0.0};
  EXPECT_EQ(RuntimeWorld::Create(non_rectangle, WorldOptions()).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(RuntimeWorldTest, RejectsInvalidLevelAndEmptyPlayerIdentity) {
  Level invalid_level = TestLevel();
  invalid_level.id.clear();
  LoadedLevelContent invalid_content = WorldContent(std::move(invalid_level));
  EXPECT_EQ(RuntimeWorld::Create(invalid_content, WorldOptions()).status().code(),
            absl::StatusCode::kInvalidArgument);

  Level level = TestLevel();
  LoadedLevelContent content = WorldContent(std::move(level));
  RuntimeWorld::Options options = WorldOptions();
  options.player_blueprint_id.clear();
  EXPECT_EQ(RuntimeWorld::Create(content, std::move(options)).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(RuntimeWorldTest, RejectsTilesetMismatchAndUnknownOccupiedTile) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  LoadedLevelContent mismatch = WorldContent(level);
  mismatch.tileset.id = "other";
  EXPECT_TRUE(absl::IsFailedPrecondition(RuntimeWorld::Create(mismatch, WorldOptions()).status()));

  PutTile(level.layers.front(), 20, 20, 99);
  LoadedLevelContent unknown_tile = WorldContent(std::move(level));
  EXPECT_TRUE(
      absl::IsFailedPrecondition(RuntimeWorld::Create(unknown_tile, WorldOptions()).status()));
}

TEST(RuntimeWorldTest, IntegratesMovementAndGroundedJumpWithoutMutatingLevel) {
  Level level = TestLevel();
  Entity player = PlayerEntity();
  player.transform.position = {48.0, 96.0};
  ASSERT_OK(level.AddEntity(0, player));
  for (int tile_x = 0; tile_x < 20; ++tile_x) PutTile(level.layers.front(), tile_x, 3);
  const Level authored = level;
  LoadedLevelContent content = WorldContent(std::move(level));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));
  ASSERT_TRUE(world->FindPlayerController(7)->grounded);

  InputSnapshot moving;
  moving.SetKeyDown(Key::kD);
  ASSERT_OK(world->StepPlayer(moving, 1.0 / 60.0, {}));
  EXPECT_GT(world->FindTransform(7)->position.x, 48.0);
  EXPECT_DOUBLE_EQ(world->FindTransform(7)->position.y, 96.0);
  EXPECT_TRUE(world->FindPlayerController(7)->grounded);

  InputSnapshot jumping = moving;
  jumping.SetKeyDown(Key::kSpace);
  ASSERT_OK(world->StepPlayer(jumping, 1.0 / 60.0, {}));
  EXPECT_LT(world->FindTransform(7)->position.y, 96.0);
  EXPECT_LT(world->FindMotion(7)->velocity.y, 0.0);
  EXPECT_FALSE(world->FindPlayerController(7)->grounded);
  EXPECT_EQ(world->level(), authored);
}

TEST(RuntimeWorldTest, InvalidStepIsTransactional) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  LoadedLevelContent content = WorldContent(std::move(level));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));
  const Transform transform = *world->FindTransform(7);
  const Motion motion = *world->FindMotion(7);
  const PlayerControllerState controller = *world->FindPlayerController(7);

  PlayerMovementConfig invalid;
  invalid.gravity = -1.0;
  EXPECT_TRUE(absl::IsInvalidArgument(world->StepPlayer({}, 1.0 / 60.0, invalid)));
  EXPECT_EQ(*world->FindTransform(7), transform);
  EXPECT_EQ(world->FindMotion(7)->velocity, motion.velocity);
  EXPECT_EQ(world->FindMotion(7)->acceleration, motion.acceleration);
  EXPECT_EQ(world->FindPlayerController(7)->intent, controller.intent);
  EXPECT_EQ(world->FindPlayerController(7)->previous_input.keys, controller.previous_input.keys);
}

TEST(RuntimeWorldTest, IdleGroundedPlayerDoesNotSlideDownSlope) {
  Level level = TestLevel();
  Entity player = PlayerEntity();
  player.transform.position = {16.0, 96.0};
  ASSERT_OK(level.AddEntity(0, player));
  PutTile(level.layers.front(), 0, 3, 2);
  LoadedLevelContent content = WorldContent(std::move(level));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(content, WorldOptions()));
  ASSERT_TRUE(world->FindPlayerController(7)->grounded);

  ASSERT_OK(world->StepPlayer({}, 1.0 / 60.0, {}));

  EXPECT_EQ(world->FindTransform(7)->position, (Vec{16, 96}));
  EXPECT_EQ(world->FindMotion(7)->velocity, (Vec{0, 0}));
  EXPECT_TRUE(world->FindPlayerController(7)->grounded);
}

}  // namespace
}  // namespace zebes
