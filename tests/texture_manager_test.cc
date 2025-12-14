#include "resources/texture_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "common/common.h"
#include "common/sdl_wrapper.h"
#include "common/utils.h"

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

class TextureManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "test_assets_" + GenerateGuid();
    std::filesystem::create_directories(test_dir_ + "/definitions/textures");
    std::filesystem::create_directories(test_dir_ + "/textures");

    mock_sdl_ = std::make_unique<MockSdlWrapper>();
    auto tm = TextureManager::Create(mock_sdl_.get(), test_dir_);
    ASSERT_TRUE(tm.ok());
    manager_ = std::move(*tm);
  }

  void TearDown() override {
    manager_.reset();
    mock_sdl_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  std::string test_dir_;
  std::unique_ptr<MockSdlWrapper> mock_sdl_;
  std::unique_ptr<TextureManager> manager_;
};

TEST_F(TextureManagerTest, CreateAndGetTexture) {
  // Create a dummy image file
  std::string img_path = test_dir_ + "/textures/test.png";
  {
    std::ofstream f(img_path);
    f << "dummy data";
  }

  // Path must be absolute for import
  auto id = manager_->CreateTexture({.path = std::filesystem::absolute(img_path).string()});
  ASSERT_TRUE(id.ok()) << id.status();

  auto tex = manager_->GetTexture(*id);
  ASSERT_TRUE(tex.ok());
  EXPECT_EQ((*tex)->id, *id);
  // Checked path should be what was saved (relative)
  EXPECT_EQ((*tex)->path, "textures/test.png");
  // Default name should be the stem
  EXPECT_EQ((*tex)->name, "test");

  // Verify JSON exists in definitions path
  std::string json_path = test_dir_ + "/definitions/textures/" + *id + ".json";
  ASSERT_TRUE(std::filesystem::exists(json_path));
}

TEST_F(TextureManagerTest, LoadAllTextures) {
  // Manually create a JSON file in definitions
  std::string json_content = R"({
    "id": "existing-texture-id",
    "path": "existing.png"
  })";

  {
    std::ofstream f(test_dir_ + "/definitions/textures/existing-texture-id.json");
    f << json_content;
    std::ofstream img(test_dir_ + "/textures/existing.png");
    img << "dummy";
  }

  auto status = manager_->LoadAllTextures();
  ASSERT_TRUE(status.ok());

  auto tex = manager_->GetTexture("existing-texture-id");
  ASSERT_TRUE(tex.ok());
  EXPECT_EQ((*tex)->id, "existing-texture-id");
  EXPECT_EQ((*tex)->name, "existing");
}

TEST_F(TextureManagerTest, UpdateTexture) {
  // Create a dummy image file
  std::string img_path = test_dir_ + "/textures/update.png";
  {
    std::ofstream f(img_path);
    f << "dummy data";
  }

  auto id = manager_->CreateTexture({.path = std::filesystem::absolute(img_path).string()});
  ASSERT_TRUE(id.ok());

  auto tex = manager_->GetTexture(*id);
  ASSERT_TRUE(tex.ok());
  EXPECT_EQ((*tex)->name, "update");

  Texture new_data = **tex;
  new_data.name = "New Name";

  auto status = manager_->UpdateTexture(new_data);
  ASSERT_TRUE(status.ok());

  // Check in-memory update
  EXPECT_EQ((*tex)->name, "New Name");

  // Reload to check persistence
  manager_->LoadAllTextures();
  auto reloaded = manager_->GetTexture(*id);
  ASSERT_TRUE(reloaded.ok());
  EXPECT_EQ((*reloaded)->name, "New Name");
}

TEST_F(TextureManagerTest, DeleteTexture) {
  // Setup
  std::string img_path = test_dir_ + "/textures/del.png";
  {
    std::ofstream f(img_path);
    f << "dummy";
  }
  auto id = manager_->CreateTexture({.path = std::filesystem::absolute(img_path).string()});
  ASSERT_TRUE(id.ok());

  // Check exists
  ASSERT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/textures/" + *id + ".json"));

  // Delete
  auto status = manager_->DeleteTexture(*id);
  ASSERT_TRUE(status.ok());

  // Check removed from manager
  auto tex = manager_->GetTexture(*id);
  EXPECT_FALSE(tex.ok());

  // Check removed from disk
  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/textures/" + *id + ".json"));
}

TEST_F(TextureManagerTest, CreateTextureWithCopy) {
  // Create a dummy image file outside the test assets directory
  std::string external_img_path = test_dir_ + "/external_image.png";
  {
    std::ofstream f(external_img_path);
    f << "dummy data";
  }

  // Path is absolute (or relative to cwd, but outside assets)
  auto id = manager_->CreateTexture({.path = external_img_path});
  ASSERT_TRUE(id.ok()) << id.status();

  auto tex = manager_->GetTexture(*id);
  ASSERT_TRUE(tex.ok());

  // Verify path is relative to assets
  EXPECT_EQ((*tex)->path, "textures/external_image.png");

  // Verify file was copied
  std::string copied_path = test_dir_ + "/textures/external_image.png";
  EXPECT_TRUE(std::filesystem::exists(copied_path));
}

TEST_F(TextureManagerTest, CreateTextureNameTooLong) {
  std::string img_path = test_dir_ + "/textures/long_name.png";
  {
    std::ofstream f(img_path);
    f << "dummy data";
  }

  Texture texture;
  texture.path = std::filesystem::absolute(img_path).string();
  texture.name = std::string(kMaxTextureNameLength + 1, 'a');

  auto result = manager_->CreateTexture(texture);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TextureManagerTest, UpdateTextureNameTooLong) {
  // Create valid texture first
  std::string img_path = test_dir_ + "/textures/valid.png";
  {
    std::ofstream f(img_path);
    f << "dummy data";
  }
  auto id = manager_->CreateTexture({.path = std::filesystem::absolute(img_path).string()});
  ASSERT_TRUE(id.ok());

  auto tex = manager_->GetTexture(*id);
  ASSERT_TRUE(tex.ok());

  Texture new_data = **tex;
  new_data.name = std::string(kMaxTextureNameLength + 1, 'a');

  auto status = manager_->UpdateTexture(new_data);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace zebes
