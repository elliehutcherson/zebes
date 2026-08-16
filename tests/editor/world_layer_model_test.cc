#include "editor/level_editor/world_layer_model.h"

#include <limits>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

TEST(WorldLayerModelTest, OpenSelectsBackmostLayerAndResetsSessionState) {
  Level level;
  level.layers.push_back(WorldLayer{.id = 4, .name = "Front"});
  WorldLayerModel model;

  model.Open(level);
  ASSERT_OK(model.SetVisible(level, 4, false));
  ASSERT_OK(model.SetLocked(level, 0, true));
  model.Open(level);

  EXPECT_EQ(model.active_layer_id(), 0);
  EXPECT_TRUE(model.IsVisible(4));
  EXPECT_FALSE(model.IsLocked(0));
}

TEST(WorldLayerModelTest, AddInsertsImmediatelyInFrontAndUsesStableId) {
  Level level;
  level.layers.push_back(WorldLayer{.id = 7, .name = "Front"});
  WorldLayerModel model;
  model.Open(level);

  ASSERT_OK_AND_ASSIGN(const int id, model.AddLayer(level));

  EXPECT_EQ(id, 8);
  ASSERT_EQ(level.layers.size(), 3u);
  EXPECT_EQ(level.layers[0].id, 0);
  EXPECT_EQ(level.layers[1].id, 8);
  EXPECT_EQ(level.layers[2].id, 7);
  EXPECT_EQ(model.active_layer_id(), 8);
}

TEST(WorldLayerModelTest, ReorderChangesDepthWithoutChangingIdentity) {
  Level level;
  level.layers.push_back(WorldLayer{.id = 1, .name = "Middle"});
  level.layers.push_back(WorldLayer{.id = 2, .name = "Front"});
  WorldLayerModel model;
  model.Open(level);

  ASSERT_OK(model.MoveForward(level, 0));
  EXPECT_EQ(level.layers[0].id, 1);
  EXPECT_EQ(level.layers[1].id, 0);
  EXPECT_TRUE(model.CanMoveBackward(level, 0));
  EXPECT_TRUE(model.CanMoveForward(level, 0));

  ASSERT_OK(model.MoveBackward(level, 0));
  EXPECT_EQ(level.layers[0].id, 0);
}

TEST(WorldLayerModelTest, DeleteSelectsANeighbourAndRefusesLastLayer) {
  Level level;
  level.layers.push_back(WorldLayer{.id = 1, .name = "Front"});
  WorldLayerModel model;
  model.Open(level);
  ASSERT_OK(model.Activate(level, 1));
  ASSERT_OK(model.SetLocked(level, 1, true));

  ASSERT_OK(model.DeleteLayer(level, 1));

  EXPECT_EQ(model.active_layer_id(), 0);
  EXPECT_FALSE(model.IsLocked(1));
  EXPECT_EQ(model.DeleteLayer(level, 0).code(), absl::StatusCode::kFailedPrecondition);
}

TEST(WorldLayerModelTest, ReconcileDropsStateForRemovedLayers) {
  Level level;
  level.layers.push_back(WorldLayer{.id = 1, .name = "Front"});
  WorldLayerModel model;
  model.Open(level);
  ASSERT_OK(model.Activate(level, 1));
  ASSERT_OK(model.SetVisible(level, 1, false));
  level.layers.pop_back();

  model.Reconcile(level);

  EXPECT_EQ(model.active_layer_id(), 0);
  EXPECT_TRUE(model.IsVisible(1));
}

TEST(WorldLayerModelTest, IdOverflowFailsWithoutChangingLayers) {
  Level level;
  level.layers.front().id = std::numeric_limits<int>::max();
  WorldLayerModel model;
  model.Open(level);

  EXPECT_EQ(model.AddLayer(level).status().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_EQ(level.layers.size(), 1u);
}

}  // namespace
}  // namespace zebes
