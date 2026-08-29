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

absl::StatusOr<std::unique_ptr<RuntimeWorld>> TestWorld() {
  Level level{
      .id = "level",
      .name = "Level",
      .tileset_id = "tileset",
      .tile_render_width = 32,
      .tile_render_height = 32,
      .width = 1024,
      .height = 1024,
  };
  const absl::Status added = level.AddEntity(0, Entity{.id = 7,
                                                       .transform = {.position = {48, 96}},
                                                       .blueprint_id = "player",
                                                       .collider_id = "collider",
                                                       .sprite_id = "idle-right"});
  if (!added.ok()) return added;
  for (int x = 0; x < 20; ++x) PutTile(level.layers.front(), x, 3);
  return RuntimeWorld::Create({
      .level = std::move(level),
      .tileset = {.id = "tileset", .tiles = {{.id = 1, .shape = TileShape::kFullBlock}}},
      .blueprints = {{"player",
                      Blueprint{
                          .id = "player",
                          .states =
                              {
                                  {.key = "idle-right",
                                   .name = "Idle right",
                                   .collider_id = "collider",
                                   .sprite_id = "idle-right"},
                                  {.key = "idle-left",
                                   .name = "Idle left",
                                   .collider_id = "collider",
                                   .sprite_id = "idle-left"},
                                  {.key = "run-right",
                                   .name = "Run right",
                                   .collider_id = "collider",
                                   .sprite_id = "run-right"},
                                  {.key = "run-left",
                                   .name = "Run left",
                                   .collider_id = "collider",
                                   .sprite_id = "run-left"},
                                  {.key = "airborne-right",
                                   .name = "Airborne right",
                                   .collider_id = "collider",
                                   .sprite_id = "airborne-right"},
                                  {.key = "airborne-left",
                                   .name = "Airborne left",
                                   .collider_id = "collider",
                                   .sprite_id = "airborne-left"},
                              },
                      }}},
      .sprites =
          {{"idle-right", Sprite{.id = "idle-right",
                                 .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
           {"idle-left", Sprite{.id = "idle-left",
                                .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
           {"run-right", Sprite{.id = "run-right",
                                .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
           {"run-left",
            Sprite{.id = "run-left", .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
           {"airborne-right", Sprite{.id = "airborne-right",
                                     .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}},
           {"airborne-left", Sprite{.id = "airborne-left",
                                    .frames = {{.frames_per_cycle = 1}, {.frames_per_cycle = 1}}}}},
      .player_blueprint_id = "player",
      .player_collider =
          {
              .id = "collider",
              .polygons = {{{-16, -64}, {16, -64}, {16, 0}, {-16, 0}}},
          },
  });
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
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world, TestWorld());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlayerSimulation> simulation,
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(world), .camera = TestCamera()}));
  EXPECT_EQ(simulation->camera().position, (Vec{48, 96}));

  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));

  const Transform* player = simulation->world().FindTransform(7);
  ASSERT_NE(player, nullptr);
  EXPECT_GT(player->position.x, 48.0);
  EXPECT_EQ(simulation->camera().position, player->position);
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 2);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "run-right");
  EXPECT_EQ(simulation->world().frame_indices().at(7), 1);
  EXPECT_EQ(FindEntity(simulation->world().level(), 7)->transform.position, (Vec{48, 96}));
}

TEST(PlayerSimulationTest, SelectsSemanticAnimationFromMovementAndRememberedFacing) {
  FixedInputManager input;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world, TestWorld());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlayerSimulation> simulation,
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(world), .camera = TestCamera()}));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 0);

  input.snapshot.SetKeyDown(Key::kA);
  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 3);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "run-left");

  input.snapshot.SetKeyDown(Key::kSpace);
  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 5);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "airborne-left");

  input.snapshot.SetKeyDown(Key::kA, false);
  input.snapshot.SetKeyDown(Key::kD);
  ASSERT_OK(simulation->Step(absl::Seconds(1.0 / 60.0)));
  EXPECT_EQ(*simulation->world().FindBlueprintStateIndex(7), 4);
  EXPECT_EQ(simulation->world().sprite_ids().at(7), "airborne-right");
}

TEST(PlayerSimulationTest, RejectsInvalidDependenciesAndDuration) {
  FixedInputManager input;
  EXPECT_TRUE(absl::IsInvalidArgument(PlayerSimulation::Create({.camera = TestCamera()}).status()));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<RuntimeWorld> world, TestWorld());
  EXPECT_TRUE(absl::IsInvalidArgument(
      PlayerSimulation::Create({.input_manager = &input, .world = std::move(world)}).status()));

  ASSERT_OK_AND_ASSIGN(world, TestWorld());
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlayerSimulation> simulation,
      PlayerSimulation::Create(
          {.input_manager = &input, .world = std::move(world), .camera = TestCamera()}));
  EXPECT_TRUE(absl::IsInvalidArgument(simulation->Step(absl::ZeroDuration())));
  EXPECT_TRUE(absl::IsInvalidArgument(simulation->Step(absl::InfiniteDuration())));
}

}  // namespace
}  // namespace zebes
