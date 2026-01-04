#include "resources/level_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>

#include "collider_manager_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sprite_manager_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class LevelManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temporary directory for testing
    std::string test_dir = "test_data/level_manager_test";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir + "/definitions/levels");

    sm_ = std::make_unique<NiceMock<SpriteManagerMock>>();
    cm_ = std::make_unique<NiceMock<ColliderManagerMock>>();

    // LevelManager::Create expects dependencies
    auto manager_or = LevelManager::Create(sm_.get(), cm_.get(), test_dir);
    ASSERT_TRUE(manager_or.ok());
    manager_ = std::move(manager_or.value());
  }

  void TearDown() override { std::filesystem::remove_all("/tmp/test_level_manager"); }

  std::unique_ptr<LevelManager> manager_;
  std::unique_ptr<SpriteManagerMock> sm_;
  std::unique_ptr<ColliderManagerMock> cm_;
};

TEST_F(LevelManagerTest, CreateAndGetLevel) {
  Level level;
  level.name = "My Level";

  EXPECT_CALL(*sm_, GetSprite(_)).Times(0);
  EXPECT_CALL(*cm_, GetCollider(_)).Times(0);

  auto id_or = manager_->CreateLevel(std::move(level));
  ASSERT_TRUE(id_or.ok()) << id_or.status();
  std::string id = *id_or;
  EXPECT_FALSE(id.empty());

  // Get
  auto loaded_or = manager_->GetLevel(id);
  ASSERT_TRUE(loaded_or.ok());
  EXPECT_EQ(loaded_or.value()->name, "My Level");
  EXPECT_EQ(loaded_or.value()->id, id);
}

TEST_F(LevelManagerTest, SerializationTest) {
  Level level;
  level.name = "Complex Level";

  // Add Parallax
  ParallaxLayer layer;
  layer.texture_id = "tex1";
  layer.scroll_factor = {0.5, 0.5};
  layer.repeat_x = true;
  level.parallax_layers.push_back(layer);

  // Add Tile Chunk
  TileChunk chunk;
  chunk.tiles[0] = 1;
  chunk.tiles[1] = 2;
  level.tile_chunks[100] = chunk;

  // Add Entity
  auto entity = std::make_unique<Entity>();
  entity->id = 123;
  entity->transform.position = {10, 20};
  entity->body.velocity = {1, 0};
  entity->body.is_static = true;

  level.AddEntity(std::move(entity));

  // Save/Create
  auto id_or = manager_->CreateLevel(std::move(level));
  ASSERT_TRUE(id_or.ok());

  // Reload
  auto loaded_or = manager_->GetLevel(id_or.value());
  ASSERT_TRUE(loaded_or.ok());
  Level* loaded = loaded_or.value();

  EXPECT_EQ(loaded->name, "Complex Level");
  ASSERT_EQ(loaded->parallax_layers.size(), 1);
  EXPECT_EQ(loaded->parallax_layers[0].texture_id, "tex1");
  EXPECT_TRUE(loaded->parallax_layers[0].repeat_x);

  ASSERT_EQ(loaded->tile_chunks.size(), 1);
  EXPECT_EQ(loaded->tile_chunks[100].tiles[0], 1);

  ASSERT_EQ(loaded->entities.size(), 1);
  EXPECT_EQ(loaded->entities[0]->id, 123);
  EXPECT_EQ(loaded->entities[0]->transform.position.x, 10);
  EXPECT_EQ(loaded->entities[0]->body.velocity.x, 1);
  EXPECT_TRUE(loaded->entities[0]->body.is_static);
}

TEST_F(LevelManagerTest, DeleteLevel) {
  Level level;
  level.name = "To Delete";
  auto id = manager_->CreateLevel(std::move(level)).value();

  ASSERT_TRUE(manager_->DeleteLevel(id).ok());
  EXPECT_FALSE(manager_->GetLevel(id).ok());
}

}  // namespace
}  // namespace zebes
