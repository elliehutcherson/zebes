#include "resources/level_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>

#include "collider_manager_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "sprite_manager_mock.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;

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
    ASSERT_OK_AND_ASSIGN(manager_, LevelManager::Create(sm_.get(), cm_.get(), test_dir));
  }

  void TearDown() override { std::filesystem::remove_all("/tmp/test_level_manager"); }

  std::unique_ptr<LevelManager> manager_;
  std::unique_ptr<SpriteManagerMock> sm_;
  std::unique_ptr<ColliderManagerMock> cm_;
};

TEST_F(LevelManagerTest, CreateAndGetLevel) {
  Level level{
      .name = "My Level",
  };

  EXPECT_CALL(*sm_, GetSprite(_)).Times(0);
  EXPECT_CALL(*cm_, GetCollider(_)).Times(0);

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  EXPECT_FALSE(id.empty());

  // Get
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));
  EXPECT_EQ(loaded->name, "My Level");
  EXPECT_EQ(loaded->id, id);
}

TEST_F(LevelManagerTest, SerializationTest) {
  Level level{
      .name = "Complex Level",
      .width = 320,
      .height = 320,
      .spawn_point = {100, 200},
  };

  // Add Parallax
  ParallaxLayer layer{
      .name = "My Layer",
      .texture_id = "tex1",
      .scroll_factor = {0.5, 0.5},
      .repeat_x = true,
  };
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

  level.AddEntity(std::move(*entity));

  // Save/Create
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Reload
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  EXPECT_EQ(loaded->name, "Complex Level");
  EXPECT_EQ(loaded->width, 320);
  EXPECT_EQ(loaded->height, 320);
  EXPECT_EQ(loaded->spawn_point.x, 100);
  EXPECT_EQ(loaded->spawn_point.y, 200);

  ASSERT_EQ(loaded->parallax_layers.size(), 1);
  EXPECT_EQ(loaded->parallax_layers[0].name, "My Layer");
  EXPECT_EQ(loaded->parallax_layers[0].texture_id, "tex1");
  EXPECT_TRUE(loaded->parallax_layers[0].repeat_x);

  ASSERT_EQ(loaded->tile_chunks.size(), 1);
  EXPECT_EQ(loaded->tile_chunks[100].tiles[0], 1);

  ASSERT_EQ(loaded->entities.size(), 1);
  const Entity& loaded_entity = loaded->entities.at(123);
  EXPECT_EQ(loaded_entity.id, 123);
  EXPECT_EQ(loaded_entity.transform.position.x, 10);
  EXPECT_EQ(loaded_entity.body.velocity.x, 1);
  EXPECT_TRUE(loaded_entity.body.is_static);
}

TEST_F(LevelManagerTest, DeleteLevel) {
  Level level{
      .name = "To Delete",
  };
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  ASSERT_TRUE(manager_->DeleteLevel(id).ok());
  EXPECT_FALSE(manager_->GetLevel(id).ok());
}

TEST_F(LevelManagerTest, ValidationTest) {
  Level level{
      .name = "Invalid",
      .width = 17,  // Not multiple of 16
      .height = 16,
  };

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Level boundaries must be multiples of tile size"));
}

TEST_F(LevelManagerTest, CreateLevel_EmptyName_Fails) {
  Level level{
      .name = "",
  };
  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("non-empty name"));
}

TEST_F(LevelManagerTest, CreateLevel_DuplicateName_Fails) {
  Level level1{
      .name = "Unique Level",
  };
  ASSERT_OK(manager_->CreateLevel(std::move(level1)));

  Level level2{
      .name = "Unique Level",
  };
  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level2));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("already exists"));
}

TEST_F(LevelManagerTest, SaveLevel_EmptyName_Fails) {
  Level level{
      .name = "Initial Name",
  };
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  loaded->name = "";  // Invalid

  absl::Status status = manager_->SaveLevel(*loaded);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("cannot be empty"));
}

TEST_F(LevelManagerTest, SaveLevel_DuplicateName_Fails) {
  Level level1{
      .name = "Level 1",
  };
  ASSERT_OK(manager_->CreateLevel(std::move(level1)));

  Level level2{
      .name = "Level 2",
  };
  ASSERT_OK_AND_ASSIGN(std::string id2, manager_->CreateLevel(std::move(level2)));

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id2));

  loaded->name = "Level 1";  // Try to rename to existing

  absl::Status status = manager_->SaveLevel(*loaded);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("already taken"));
}

TEST_F(LevelManagerTest, SaveLevel_EmptyParallaxName_Fails) {
  Level level{
      .name = "Parallax Test",
  };
  ParallaxLayer layer{
      .name = "",  // Invalid
      .texture_id = "tex",
  };
  level.parallax_layers.push_back(layer);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  // CreateLevel calls SaveLevel, so it should fail there too
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Parallax layer name cannot be empty"));
}

TEST_F(LevelManagerTest, SaveLevel_DuplicateParallaxName_Fails) {
  Level level{
      .name = "Parallax Dupe Test",
  };

  ParallaxLayer l1{
      .name = "Background",
      .texture_id = "t1",
  };

  ParallaxLayer l2{
      .name = "Background",  // Duplicate
      .texture_id = "t2",
  };

  level.parallax_layers = {l1, l2};

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Duplicate parallax layer name"));
}

TEST_F(LevelManagerTest, ParallaxLayerPersistence) {
  Level level{
      .name = "Parallax Persistence Level",
  };

  ParallaxLayer layer1{
      .name = "Layer 1",
      .texture_id = "sky_tex",
      .scroll_factor = {0.1, 0.1},
      .repeat_x = true,
  };

  ParallaxLayer layer2{
      .name = "Layer 2",
      .texture_id = "mountains_tex",
      .scroll_factor = {0.5, 0.2},
      .repeat_x = false,
  };

  level.parallax_layers = {layer1, layer2};

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Force reload from disk
  manager_ = nullptr;
  ASSERT_OK_AND_ASSIGN(auto new_manager,
                       LevelManager::Create(sm_.get(), cm_.get(), "test_data/level_manager_test"));
  manager_ = std::move(new_manager);
  ASSERT_OK(manager_->LoadAllLevels());

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->parallax_layers.size(), 2);

  const ParallaxLayer& l1 = loaded->parallax_layers[0];
  EXPECT_EQ(l1.name, "Layer 1");
  EXPECT_EQ(l1.texture_id, "sky_tex");
  EXPECT_EQ(l1.scroll_factor.x, 0.1);
  EXPECT_EQ(l1.scroll_factor.y, 0.1);
  EXPECT_TRUE(l1.repeat_x);

  const ParallaxLayer& l2 = loaded->parallax_layers[1];
  EXPECT_EQ(l2.name, "Layer 2");
  EXPECT_EQ(l2.texture_id, "mountains_tex");
  EXPECT_EQ(l2.scroll_factor.x, 0.5);
  EXPECT_EQ(l2.scroll_factor.y, 0.2);
  EXPECT_FALSE(l2.repeat_x);
}

}  // namespace
}  // namespace zebes
