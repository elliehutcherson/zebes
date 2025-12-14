#include "resources/sprite_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "common/sdl_wrapper.h"
#include "common/utils.h"
#include "resources/texture_manager.h"

namespace zebes {
namespace {

class MockSdlWrapper : public SdlWrapper {
 public:
  MockSdlWrapper() : SdlWrapper(nullptr, nullptr) {}

  absl::StatusOr<SDL_Texture*> CreateTexture(const std::string& path) override {
    // Return a dummy pointer
    return reinterpret_cast<SDL_Texture*>(0x1234);
  }

  void DestroyTexture(SDL_Texture* texture) override {
    // No-op
  }
};

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

    mock_sdl_ = std::make_unique<MockSdlWrapper>();
    // Create Texture Manager
    auto tm = TextureManager::Create(mock_sdl_.get(), test_dir_);
    ASSERT_TRUE(tm.ok());
    texture_manager_ = std::move(*tm);

    // Create manager
    auto sm = SpriteManager::Create(texture_manager_.get(), test_dir_);
    ASSERT_TRUE(sm.ok());
    manager_ = std::move(*sm);
  }

  void TearDown() override {
    manager_.reset();
    texture_manager_.reset();
    mock_sdl_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  std::string test_dir_;
  std::unique_ptr<MockSdlWrapper> mock_sdl_;
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
  auto tex_id_or = texture_manager_->CreateTexture({.path = "texture.png"});
  ASSERT_TRUE(tex_id_or.ok()) << tex_id_or.status();
  sprite.texture_id = *tex_id_or;

  auto id_or = manager_->CreateSprite(sprite);
  ASSERT_TRUE(id_or.ok()) << id_or.status();
  std::string id = *id_or;
  EXPECT_FALSE(id.empty());

  auto sprite_or = manager_->GetSprite(id);
  ASSERT_TRUE(sprite_or.ok());
  Sprite* loaded_sprite = *sprite_or;
  EXPECT_EQ(loaded_sprite->id, id);
  EXPECT_EQ(loaded_sprite->name, "TestSprite");

  // Verify file exists
  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/sprites/" + id + ".json"));
}

TEST_F(SpriteManagerTest, LoadAllSprites) {
  // Create dummy texture
  std::string tex_path = test_dir_ + "/textures/tex.png";
  std::ofstream f(tex_path);

  auto tex_id_or = texture_manager_->CreateTexture({.path = "tex.png"});
  ASSERT_TRUE(tex_id_or.ok());
  std::string tex_id = *tex_id_or;

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
  auto status = manager_->LoadAllSprites();
  ASSERT_TRUE(status.ok()) << status;

  auto sprite_or = manager_->GetSprite(id);
  ASSERT_TRUE(sprite_or.ok());
  EXPECT_EQ((*sprite_or)->name, "Manual");
}

TEST_F(SpriteManagerTest, UpdateSprite) {
  Sprite sprite;
  sprite.name = "Initial";

  std::string tex_path = test_dir_ + "/textures/t.png";
  std::ofstream f(tex_path);

  auto tex_id_or = texture_manager_->CreateTexture({.path = "t.png"});
  ASSERT_TRUE(tex_id_or.ok());
  sprite.texture_id = *tex_id_or;

  auto id_or = manager_->CreateSprite(sprite);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  // Update
  sprite = *(*manager_->GetSprite(id));
  sprite.name = "Updated";
  auto status = manager_->SaveSprite(sprite);
  ASSERT_TRUE(status.ok());

  // Check
  auto sprite_or = manager_->GetSprite(id);
  EXPECT_EQ((*sprite_or)->name, "Updated");
}

TEST_F(SpriteManagerTest, DeleteSprite) {
  Sprite sprite;
  auto tex_path = test_dir_ + "/textures/t2.png";
  std::ofstream f(tex_path);

  auto tex_id_or = texture_manager_->CreateTexture({.path = "t2.png"});
  ASSERT_TRUE(tex_id_or.ok());
  sprite.texture_id = *tex_id_or;

  auto id_or = manager_->CreateSprite(sprite);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  auto status = manager_->DeleteSprite(id);
  ASSERT_TRUE(status.ok());

  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/sprites/" + id + ".json"));
  EXPECT_FALSE(manager_->GetSprite(id).ok());
}

}  // namespace
}  // namespace zebes
