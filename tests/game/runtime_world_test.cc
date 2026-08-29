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
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
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
      .collider_id = kPlayerColliderId,
  };
}

Level TestLevel() {
  return {
      .id = "test-level",
      .name = "Test Level",
      .width = 1024.0,
      .height = 1024.0,
      .spawn_point = {0.0, 0.0},
  };
}

RuntimeWorld::Options WorldOptions(Level level) {
  return {
      .level = std::move(level),
      .player_blueprint_id = kPlayerBlueprintId,
      .player_collider = PlayerCollider(),
  };
}

TEST(RuntimeWorldTest, OwnsRuntimeStateWithoutMutatingAuthoredLevel) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  Entity static_entity = PlayerEntity(8);
  static_entity.blueprint_id = "static-decoration";
  static_entity.collider_id.clear();
  static_entity.body.is_static = true;
  ASSERT_OK(level.AddEntity(0, static_entity));
  const Level authored_level = level;

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(WorldOptions(std::move(level))));

  EXPECT_EQ(world->level(), authored_level);
  EXPECT_EQ(world->player_entity_id(), 7);
  EXPECT_EQ(world->player_local_collider(),
            (AxisAlignedBox{.min = {-16.0, -64.0}, .max = {16.0, 0.0}}));
  ASSERT_NE(world->FindTransform(7), nullptr);
  EXPECT_EQ(world->FindTransform(7)->position, (Vec{128.0, 192.0}));
  EXPECT_NE(world->FindMotion(7), nullptr);
  EXPECT_NE(world->FindPlayerController(7), nullptr);
  EXPECT_NE(world->FindTransform(8), nullptr);
  EXPECT_EQ(world->FindMotion(8), nullptr);
  EXPECT_EQ(world->FindTransform(999), nullptr);

  world->FindTransform(7)->position.x = 400.0;
  EXPECT_DOUBLE_EQ(world->FindTransform(7)->position.x, 400.0);
  EXPECT_DOUBLE_EQ(FindEntity(world->level(), 7)->transform.position.x, 128.0);
}

TEST(RuntimeWorldTest, ConsumesJumpEdgeOnceAcrossCatchUpTicks) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world,
                       RuntimeWorld::Create(WorldOptions(std::move(level))));
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
  EXPECT_EQ(RuntimeWorld::Create(WorldOptions(level)).status().code(), absl::StatusCode::kNotFound);

  ASSERT_OK(level.AddEntity(0, PlayerEntity()));
  level.layers.push_back({.id = 1, .name = "Other"});
  ASSERT_OK(level.AddEntity(1, PlayerEntity(8)));
  EXPECT_EQ(RuntimeWorld::Create(WorldOptions(std::move(level))).status().code(),
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

    const absl::Status status = RuntimeWorld::Create(WorldOptions(std::move(level))).status();
    const absl::StatusCode expected_code =
        variation == 4 ? absl::StatusCode::kInvalidArgument : absl::StatusCode::kFailedPrecondition;
    EXPECT_EQ(status.code(), expected_code) << "variation " << variation;
  }
}

TEST(RuntimeWorldTest, RejectsInvalidPlayerCollider) {
  Level level = TestLevel();
  ASSERT_OK(level.AddEntity(0, PlayerEntity()));

  RuntimeWorld::Options empty_id = WorldOptions(level);
  empty_id.player_collider.id.clear();
  EXPECT_EQ(RuntimeWorld::Create(std::move(empty_id)).status().code(),
            absl::StatusCode::kInvalidArgument);

  RuntimeWorld::Options wrong_size = WorldOptions(level);
  wrong_size.player_collider.polygons.front()[0].y = -63.0;
  wrong_size.player_collider.polygons.front()[1].y = -63.0;
  EXPECT_EQ(RuntimeWorld::Create(std::move(wrong_size)).status().code(),
            absl::StatusCode::kFailedPrecondition);

  RuntimeWorld::Options missing_polygon = WorldOptions(level);
  missing_polygon.player_collider.polygons.clear();
  EXPECT_EQ(RuntimeWorld::Create(std::move(missing_polygon)).status().code(),
            absl::StatusCode::kFailedPrecondition);

  RuntimeWorld::Options non_finite = WorldOptions(level);
  non_finite.player_collider.polygons.front()[0].x = std::nan("");
  EXPECT_EQ(RuntimeWorld::Create(std::move(non_finite)).status().code(),
            absl::StatusCode::kInvalidArgument);

  RuntimeWorld::Options non_rectangle = WorldOptions(std::move(level));
  non_rectangle.player_collider.polygons.front()[3] = {0.0, 0.0};
  EXPECT_EQ(RuntimeWorld::Create(std::move(non_rectangle)).status().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST(RuntimeWorldTest, RejectsInvalidLevelAndEmptyPlayerIdentity) {
  Level invalid_level = TestLevel();
  invalid_level.id.clear();
  EXPECT_EQ(RuntimeWorld::Create(WorldOptions(std::move(invalid_level))).status().code(),
            absl::StatusCode::kInvalidArgument);

  Level level = TestLevel();
  RuntimeWorld::Options options = WorldOptions(std::move(level));
  options.player_blueprint_id.clear();
  EXPECT_EQ(RuntimeWorld::Create(std::move(options)).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
