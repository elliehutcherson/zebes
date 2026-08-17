#include "resources/sprite_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "common/utils.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "resources/fake_texture_resource_store.h"
#include "resources/texture_manager.h"

namespace zebes {
namespace {

class SpriteManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "test_sprite_assets_" + GenerateGuid();
    std::filesystem::remove_all(test_dir_);  // Ensure clean

    // Setup structure for TextureManager
    std::filesystem::create_directories(test_dir_ + "/definitions/textures");
    std::filesystem::create_directories(test_dir_ + "/textures");

    // Setup structure for SpriteManager
    std::filesystem::create_directories(test_dir_ + "/definitions/sprites");

    resources_ = std::make_unique<FakeTextureResourceStore>();
    // Create Texture Manager
    ASSERT_OK_AND_ASSIGN(texture_manager_, TextureManager::Create(resources_.get(), test_dir_));

    // Create manager
    ASSERT_OK_AND_ASSIGN(manager_, SpriteManager::Create(texture_manager_.get(), test_dir_));
  }

  void TearDown() override {
    manager_.reset();
    texture_manager_.reset();
    resources_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  std::string test_dir_;
  std::unique_ptr<FakeTextureResourceStore> resources_;
  std::unique_ptr<TextureManager> texture_manager_;
  std::unique_ptr<SpriteManager> manager_;
};

TEST_F(SpriteManagerTest, CreateAndGetSprite) {
  Sprite sprite;
  sprite.name = "TestSprite";

  // Create dummy texture file
  std::string tex_path = test_dir_ + "/textures/texture.png";
  std::ofstream f(tex_path);

  // Create a dummy texture first so we have a valid ID
  // Note: TextureManager expects path relative to images dir
  ASSERT_OK_AND_ASSIGN(sprite.texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));
  EXPECT_FALSE(id.empty());

  ASSERT_OK_AND_ASSIGN(Sprite * loaded_sprite, manager_->GetSprite(id));
  EXPECT_EQ(loaded_sprite->id, id);
  EXPECT_EQ(loaded_sprite->name, "TestSprite");

  // Verify file exists
  EXPECT_TRUE(
      std::filesystem::exists(test_dir_ + "/definitions/sprites/TestSprite-" + id + ".json"));
}

TEST_F(SpriteManagerTest, CreateSpriteWithIdKeepsThePreparedIdentity) {
  std::string tex_path = test_dir_ + "/textures/prepared.png";
  std::ofstream file(tex_path);
  ASSERT_OK_AND_ASSIGN(const std::string texture_id,
                       texture_manager_->CreateTexture({.path = tex_path}));
  Sprite sprite{
      .id = "prepared-sprite-1",
      .name = "Cave boulder",
      .texture_id = texture_id,
  };

  ASSERT_OK(manager_->CreateSpriteWithId(sprite));
  ASSERT_OK_AND_ASSIGN(Sprite * loaded, manager_->GetSprite(sprite.id));
  EXPECT_EQ(*loaded, sprite);
  EXPECT_EQ(manager_->CreateSpriteWithId(sprite).code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(SpriteManagerTest, LoadAllSprites) {
  // Create dummy texture
  std::string tex_path = test_dir_ + "/textures/tex.png";
  std::ofstream f(tex_path);

  ASSERT_OK_AND_ASSIGN(std::string tex_id, texture_manager_->CreateTexture({.path = tex_path}));

  // Manually create a JSON file
  std::string id = "manual-id";
  // JSON format for Sprite: id, name, texture_id, frames
  std::string json_content = R"({
    "id": "manual-id",
    "name": "Manual",
    "texture_id": ")" + tex_id +
                             R"(",
    "frames": []
  })";

  {
    std::ofstream f(test_dir_ + "/definitions/sprites/" + id + ".json");
    f << json_content;
  }

  // Load
  ASSERT_OK(manager_->LoadAllSprites());

  ASSERT_OK_AND_ASSIGN(Sprite * loaded, manager_->GetSprite(id));
  EXPECT_EQ(loaded->name, "Manual");
}

TEST_F(SpriteManagerTest, UpdateSprite) {
  Sprite sprite;
  sprite.name = "Initial";

  std::string tex_path = test_dir_ + "/textures/t.png";
  std::ofstream f(tex_path);

  ASSERT_OK_AND_ASSIGN(sprite.texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));

  // Update
  ASSERT_OK_AND_ASSIGN(Sprite * stored, manager_->GetSprite(id));
  sprite = *stored;
  sprite.name = "Updated";
  ASSERT_OK(manager_->SaveSprite(sprite));

  // Check
  ASSERT_OK_AND_ASSIGN(Sprite * updated, manager_->GetSprite(id));
  EXPECT_EQ(updated->name, "Updated");
}

// Editors hold a Sprite* for as long as they are editing it. Replacing the
// cached unique_ptr on save freed what they held.
TEST_F(SpriteManagerTest, SavingKeepsPointersHandedOutBeforeIt) {
  std::string tex_path = test_dir_ + "/textures/stable.png";
  std::ofstream file(tex_path);
  ASSERT_OK_AND_ASSIGN(std::string texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  Sprite sprite;
  sprite.name = "Initial";
  sprite.texture_id = texture_id;
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));
  ASSERT_OK_AND_ASSIGN(Sprite * held, manager_->GetSprite(id));

  Sprite edited = *held;
  edited.name = "Renamed";
  ASSERT_OK(manager_->SaveSprite(edited));

  ASSERT_OK_AND_ASSIGN(Sprite * after, manager_->GetSprite(id));
  EXPECT_EQ(after, held) << "the address a caller is still holding must survive a save";
  EXPECT_EQ(held->name, "Renamed");
}

TEST_F(SpriteManagerTest, DeleteSprite) {
  Sprite sprite;
  auto tex_path = test_dir_ + "/textures/t2.png";
  std::ofstream f(tex_path);

  ASSERT_OK_AND_ASSIGN(sprite.texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));

  ASSERT_OK(manager_->DeleteSprite(id));

  EXPECT_FALSE(
      std::filesystem::exists(test_dir_ + "/definitions/sprites/TestSprite-" + id + ".json"));
  EXPECT_FALSE(manager_->GetSprite(id).ok());
}

TEST_F(SpriteManagerTest, SaveAndLoadSpriteWithFrames) {
  Sprite sprite;
  sprite.name = "FrameSprite";

  std::string tex_path = test_dir_ + "/textures/f.png";
  std::ofstream f(tex_path);

  ASSERT_OK_AND_ASSIGN(sprite.texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  // Add frames
  SpriteFrame frame1;
  frame1.index = 0;
  frame1.texture_x = 10;
  frame1.texture_y = 20;
  frame1.texture_w = 32;
  frame1.texture_h = 32;

  SpriteFrame frame2;
  frame2.index = 1;
  frame2.texture_x = 42;
  frame2.texture_y = 20;
  frame2.texture_w = 32;
  frame2.texture_h = 32;

  sprite.frames.push_back(frame1);
  sprite.frames.push_back(frame2);

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));

  // Reload using a new manager to simulate restart
  ASSERT_OK_AND_ASSIGN(auto sm2, SpriteManager::Create(texture_manager_.get(), test_dir_));

  // New manager needs to load everything or just load the specific sprite
  ASSERT_OK(sm2->LoadAllSprites());
  ASSERT_OK_AND_ASSIGN(Sprite * loaded, sm2->GetSprite(id));

  // Verify JSON on disk
  std::ifstream f2(test_dir_ + "/definitions/sprites/FrameSprite-" + id + ".json");
  nlohmann::json j_out;
  f2 >> j_out;
  EXPECT_TRUE(j_out.contains("frames"));
  EXPECT_EQ(j_out["frames"].size(), 2);

  EXPECT_EQ(loaded->frames.size(), 2);
  if (loaded->frames.size() >= 2) {
    EXPECT_EQ(loaded->frames[0].texture_x, 10);
    EXPECT_EQ(loaded->frames[1].texture_x, 42);
  }
}

TEST_F(SpriteManagerTest, LoadPartialSpriteFrame) {
  // Test robustness with missing fields in JSON
  std::string tex_path = test_dir_ + "/textures/partial.png";
  std::ofstream f(tex_path);

  ASSERT_OK_AND_ASSIGN(std::string tex_id, texture_manager_->CreateTexture({.path = tex_path}));

  std::string id = "partial-id";
  // Missing texture_w, texture_h, etc.
  std::string json_content = R"({
    "id": "partial-id",
    "name": "Partial",
    "texture_id": ")" + tex_id +
                             R"(",
    "frames": [
       { "index": 0, "texture_x": 123 }
    ]
  })";

  {
    std::ofstream f(test_dir_ + "/definitions/sprites/" + id + ".json");
    f << json_content;
  }

  // A bulk load reads every file so one bad definition cannot hide the others,
  // then reports what it could not read. Returning OK here would make the
  // sprite vanish from the catalog with nothing but a terminal warning to say
  // why, which is the failure strict parsing exists to surface.
  const absl::Status loaded = manager_->LoadAllSprites();
  EXPECT_FALSE(loaded.ok());
  EXPECT_THAT(std::string(loaded.message()), ::testing::HasSubstr("partial-id"));

  auto sprite_or = manager_->GetSprite(id);
  // It should NOT be found
  EXPECT_FALSE(sprite_or.ok());
}

TEST_F(SpriteManagerTest, SaveAndLoadSpriteWithOffsets) {
  Sprite sprite;
  sprite.name = "OffsetSprite";

  std::string tex_path = test_dir_ + "/textures/off.png";
  std::ofstream f(tex_path);

  ASSERT_OK_AND_ASSIGN(sprite.texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  // Add frames with offsets
  SpriteFrame frame1;
  frame1.index = 0;
  frame1.texture_x = 0;
  frame1.texture_y = 0;
  frame1.texture_w = 32;
  frame1.texture_h = 32;
  frame1.offset_x = 10;
  frame1.offset_y = -5;

  sprite.frames.push_back(frame1);

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));

  // Reload using a new manager to simulate restart
  ASSERT_OK_AND_ASSIGN(auto sm2, SpriteManager::Create(texture_manager_.get(), test_dir_));

  ASSERT_OK(sm2->LoadAllSprites());
  ASSERT_OK_AND_ASSIGN(Sprite * loaded, sm2->GetSprite(id));

  EXPECT_EQ(loaded->frames.size(), 1);
  if (loaded->frames.size() >= 1) {
    EXPECT_EQ(loaded->frames[0].offset_x, 10);
    EXPECT_EQ(loaded->frames[0].offset_y, -5);
  }
}

TEST_F(SpriteManagerTest, RenameSprite) {
  Sprite sprite;
  sprite.name = "OldName";

  std::string tex_path = test_dir_ + "/textures/rename.png";
  std::ofstream f(tex_path);
  ASSERT_OK_AND_ASSIGN(sprite.texture_id, texture_manager_->CreateTexture({.path = tex_path}));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateSprite(sprite));

  std::string old_file = test_dir_ + "/definitions/sprites/OldName-" + id + ".json";
  ASSERT_TRUE(std::filesystem::exists(old_file));

  // Rename
  sprite.id = id;
  sprite.name = "NewName";
  ASSERT_OK(manager_->SaveSprite(sprite));

  std::string new_file = test_dir_ + "/definitions/sprites/NewName-" + id + ".json";
  EXPECT_TRUE(std::filesystem::exists(new_file));
  EXPECT_FALSE(std::filesystem::exists(old_file));
}

}  // namespace
}  // namespace zebes
