#include "game/player_simulation.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "engine/input_manager_interface.h"
#include "engine/input_types.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/blueprint.h"
#include "objects/camera.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

class FixedInputManager final : public IInputManager {
 public:
  void BindAction(absl::string_view, Key) override {}
  void Update() override {}
  bool IsActionActive(absl::string_view) const override { return false; }
  bool IsActionJustPressed(absl::string_view) const override { return false; }
  InputSnapshot CurrentSnapshot() const override { return snapshot; }
  bool QuitRequested() const override { return false; }

  InputSnapshot snapshot;
};

void PutTile(WorldLayer& layer, int tile_x, int tile_y) {
  const int chunk_x = tile_x / TileChunk::kSize;
  const int chunk_y = tile_y / TileChunk::kSize;
  const int local_x = tile_x % TileChunk::kSize;
  const int local_y = tile_y % TileChunk::kSize;
  layer.tile_chunks[ChunkKey(chunk_x, chunk_y)].tiles[local_y * TileChunk::kSize + local_x] = 1;
}

struct TestRuntimeWorld {
  std::unique_ptr<LoadedLevelContent> content;
  std::unique_ptr<RuntimeWorld> world;
};

absl::StatusOr<TestRuntimeWorld> TestWorld() {
  auto content = std::make_unique<LoadedLevelContent>();
  content->level = Level{
      .id = "level",
      .name = "Level",
      .tileset_id = "tileset",
      .tile_render_width = 32,
      .tile_render_height = 32,
      .width = 1024,
      .height = 1024,
  };
  const absl::Status added = content->level.AddEntity(0, Entity{.id = 7,
                                                                .transform = {.position = {48, 96}},
                                                                .blueprint_id = "player",
                                                                .blueprint_state_key = "idle-right",
                                                                .sprite_id = "idle-right",
                                                                .collider_id = "collider"});
  if (!added.ok()) return added;
  for (int x = 0; x < 20; ++x) PutTile(content->level.layers.front(), x, 3);
  content->tileset = {.id = "tileset", .tiles = {{.id = 1, .shape = TileShape::kFullBlock}}};
  content->blueprints = {{"player", Blueprint{
                                        .id = "player",
                                        .states =
                                            {
                                                {.key = "run-left",
                                                 .name = "Run left",
                                                 .collider_id = "collider",
                                                 .sprite_id = "run-left"},
                                                {.key = "airborne-right",
                                                 .name = "Airborne right",
                                                 .collider_id = "collider",
                                                 .sprite_id = "airborne-right"},
                                                {.key = "idle-left",
                                                 .name = "Idle left",
                                                 .collider_id = "collider",
                                                 .sprite_id = "idle-left"},
                                                {.key = "airborne-left",
                                                 .name = "Airborne left",
                                                 .collider_id = "collider",
                                                 .sprite_id = "airborne-left"},
                                                {.key = "run-right",
                                                 .name = "Run right",
                                                 .collider_id = "collider",
                                                 .sprite_id = "run-right"},
                                                {.key = "idle-right",
                                                 .name = "Idle right",
                                                 .collider_id = "collider",
                                                 .sprite_id = "idle-right"},
                                            },
                                    }}};
  content->sprites = {
      {"idle-right",
       Sprite{.id = "idle-right", .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
      {"idle-left",
       Sprite{.id = "idle-left", .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
      {"run-right",
       Sprite{.id = "run-right", .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
      {"run-left",
       Sprite{.id = "run-left", .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
      {"airborne-right", Sprite{.id = "airborne-right",
                                .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
      {"airborne-left", Sprite{.id = "airborne-left",
                               .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}}};
  content->colliders = {{"collider", Collider{
                                         .id = "collider",
                                         .polygons = {{{-16, -64}, {16, -64}, {16, 0}, {-16, 0}}},
                                     }}};
  absl::StatusOr<std::unique_ptr<RuntimeWorld>> world =
      RuntimeWorld::Create(*content, {.player_blueprint_id = "player"});
  if (!world.ok()) return world.status();
  return TestRuntimeWorld{.content = std::move(content), .world = *std::move(world)};
}

Camera TestCamera() {
  return {
      .position = {0, 0},
      .zoom = 1.0,
      .viewport_width = 960,
      .viewport_height = 540,
  };
}

TEST(PlayerSimulationTest, AdvancesPlayerAndFollowsCommittedTransform) {
  FixedInputManager input;
  input.snapshot.SetKeyDown(Key::kD);
  ASSERT_OK_AND_ASSIGN(TestRuntimeWorld test_world, TestWorld());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlayerSimulation> simulation,
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(test_world.world), .camera = TestCamera()}));
  EXPECT_EQ(simulation->camera().position, (Vec{48, 96}));

  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));

  const Transform* player = simulation->world().FindTransform(7);
  ASSERT_NE(player, nullptr);
  EXPECT_GT(player->position.x, 48.0);
  EXPECT_EQ(simulation->camera().position, player->position);
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 4);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "run-right");
  EXPECT_EQ(simulation->world().frame_indices().at(7), 1);
  EXPECT_EQ(FindEntity(simulation->world().level(), 7)->transform.position, (Vec{48, 96}));
}

TEST(PlayerSimulationTest, SelectsSemanticAnimationFromMovementAndRememberedFacing) {
  FixedInputManager input;
  ASSERT_OK_AND_ASSIGN(TestRuntimeWorld test_world, TestWorld());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlayerSimulation> simulation,
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(test_world.world), .camera = TestCamera()}));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 5);

  input.snapshot.SetKeyDown(Key::kA);
  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 0);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "run-left");

  input.snapshot.SetKeyDown(Key::kSpace);
  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 3);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "airborne-left");

  input.snapshot.SetKeyDown(Key::kA, false);
  input.snapshot.SetKeyDown(Key::kD);
  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 1);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "airborne-right");
}

TEST(PlayerSimulationTest, RejectsInvalidDependenciesAndDuration) {
  FixedInputManager input;
  EXPECT_TRUE(absl::IsInvalidArgument(PlayerSimulation::Create({.camera = TestCamera()}).status()));
  ASSERT_OK_AND_ASSIGN(TestRuntimeWorld invalid_camera_world, TestWorld());
  EXPECT_TRUE(absl::IsInvalidArgument(
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(invalid_camera_world.world)})
          .status()));

  ASSERT_OK_AND_ASSIGN(TestRuntimeWorld test_world, TestWorld());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlayerSimulation> simulation,
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(test_world.world), .camera = TestCamera()}));
  EXPECT_TRUE(absl::IsInvalidArgument(simulation->Step(absl::ZeroDuration())));
  EXPECT_TRUE(absl::IsInvalidArgument(simulation->Step(absl::InfiniteDuration())));
}

}  // namespace
}  // namespace zebes
