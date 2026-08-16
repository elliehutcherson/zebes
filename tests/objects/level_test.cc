#include "objects/level.h"

#include "absl/status/status.h"
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

}  // namespace
}  // namespace zebes
