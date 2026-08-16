#include "resources/texture_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "common/common.h"
#include "common/utils.h"
#include "macros.h"
#include "resources/fake_texture_resource_store.h"

namespace zebes {

// Exposes the protected SaveTexture method for testing via friendship.
class TextureManagerTestPeer {
 public:
  static absl::Status SaveTexture(TextureManager& manager, const Texture& texture) {
    return manager.SaveTexture(texture);
  }
};

namespace {

class TextureManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "test_assets_" + GenerateGuid();
    std::filesystem::create_directories(test_dir_ + "/definitions/textures");
    std::filesystem::create_directories(test_dir_ + "/textures");

    resources_ = std::make_unique<FakeTextureResourceStore>();
    ASSERT_OK_AND_ASSIGN(manager_, TextureManager::Create(resources_.get(), test_dir_));
  }

  void TearDown() override {
    manager_.reset();
    resources_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  std::string test_dir_;
  std::unique_ptr<FakeTextureResourceStore> resources_;
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
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTexture(
                                           {.path = std::filesystem::absolute(img_path).string()}));

  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture(id));
  EXPECT_EQ(tex->id, id);
  // Checked path should be what was saved (relative)
  EXPECT_EQ(tex->path, "textures/test.png");
  // Default name should be the stem
  EXPECT_EQ(tex->name, "test");
  // The runtime handle lives on the manager, not on the definition.
  ASSERT_OK_AND_ASSIGN(TextureHandle handle, manager_->GetTextureHandle(id));
  EXPECT_TRUE(handle);
  ASSERT_EQ(resources_->loaded_paths.size(), 1);
  EXPECT_EQ(resources_->loaded_paths.front(), test_dir_ + "/textures/test.png");

  // Verify JSON exists in definitions path
  std::string json_path = test_dir_ + "/definitions/textures/test-" + id + ".json";
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

  ASSERT_OK(manager_->LoadAllTextures());

  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture("existing-texture-id"));
  EXPECT_EQ(tex->id, "existing-texture-id");
  EXPECT_EQ(tex->name, "existing");
}

TEST_F(TextureManagerTest, UpdateTexture) {
  // Create a dummy image file
  std::string img_path = test_dir_ + "/textures/update.png";
  {
    std::ofstream f(img_path);
    f << "dummy data";
  }

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTexture(
                                           {.path = std::filesystem::absolute(img_path).string()}));

  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture(id));
  EXPECT_EQ(tex->name, "update");

  Texture new_data = *tex;
  new_data.name = "New Name";

  ASSERT_OK(manager_->UpdateTexture(new_data));

  // Check in-memory update
  EXPECT_EQ(tex->name, "New Name");

  // Reload to check persistence
  EXPECT_OK(manager_->LoadAllTextures());
  ASSERT_OK_AND_ASSIGN(Texture * reloaded, manager_->GetTexture(id));
  EXPECT_EQ(reloaded->name, "New Name");

  // Check file was renamed
  // Old name "update", New name "New Name"
  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/textures/update-" + id + ".json"));
  EXPECT_TRUE(
      std::filesystem::exists(test_dir_ + "/definitions/textures/New Name-" + id + ".json"));
}

TEST_F(TextureManagerTest, DeleteTexture) {
  // Setup
  std::string img_path = test_dir_ + "/textures/del.png";
  {
    std::ofstream f(img_path);
    f << "dummy";
  }
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTexture(
                                           {.path = std::filesystem::absolute(img_path).string()}));

  // Check exists
  ASSERT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/textures/del-" + id + ".json"));

  // Delete
  ASSERT_OK(manager_->DeleteTexture(id));
  ASSERT_EQ(resources_->unloaded_ids.size(), 1);
  EXPECT_NE(resources_->unloaded_ids.front(), 0);

  // Check removed from manager
  auto tex = manager_->GetTexture(id);
  EXPECT_FALSE(tex.ok());

  // Check removed from disk
  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/textures/del-" + id + ".json"));

  // The artwork goes with it. A file left in textures/ that no definition names
  // is unreachable and indistinguishable from art still in use.
  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/textures/del.png"));
}

// Generated artwork is written by the manager rather than copied in, and is
// deleted on the same terms.
TEST_F(TextureManagerTest, DeletingGeneratedArtworkRemovesTheImageItWrote) {
  const std::vector<uint8_t> pixels(2 * 2 * 4, 255);
  ASSERT_OK_AND_ASSIGN(const std::string id,
                       manager_->CreateTextureFromPixels("generated", 2, 2, pixels));
  ASSERT_TRUE(std::filesystem::exists(test_dir_ + "/textures/generated.png"));

  ASSERT_OK(manager_->DeleteTexture(id));

  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/textures/generated.png"));
  EXPECT_FALSE(
      std::filesystem::exists(test_dir_ + "/definitions/textures/generated-" + id + ".json"));
}

// Generated artwork has no file to import from, so the manager has to write one
// before a definition can point at it.
TEST_F(TextureManagerTest, CreateTextureFromPixelsWritesArtworkAndRegistersIt) {
  const std::vector<uint8_t> pixels(4 * 4 * 4, 0xAB);

  ASSERT_OK_AND_ASSIGN(std::string id,
                       manager_->CreateTextureFromPixels("generated", 4, 4, pixels));

  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/textures/generated.png"));

  ASSERT_OK_AND_ASSIGN(Texture * texture, manager_->GetTexture(id));
  EXPECT_EQ(texture->name, "generated");
  EXPECT_EQ(texture->path, "textures/generated.png");
}

// Silently replacing artwork would repoint every tileset already using it at a
// different picture, which is exactly the failure the source_art split exists
// to prevent.
TEST_F(TextureManagerTest, CreateTextureFromPixelsRefusesToReplaceExistingArtwork) {
  const std::vector<uint8_t> pixels(4 * 4 * 4, 0xAB);
  ASSERT_OK(manager_->CreateTextureFromPixels("generated", 4, 4, pixels));

  absl::Status status = manager_->CreateTextureFromPixels("generated", 4, 4, pixels).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kAlreadyExists);
}

TEST_F(TextureManagerTest, ReplaceTexturePixelsKeepsIdentityAndReloadsTheRuntimeHandle) {
  const std::vector<uint8_t> original(4 * 4 * 4, 0x22);
  ASSERT_OK_AND_ASSIGN(const std::string id,
                       manager_->CreateTextureFromPixels("generated", 4, 4, original));
  ASSERT_OK_AND_ASSIGN(Texture * texture_before, manager_->GetTexture(id));
  const Texture definition_before = *texture_before;
  ASSERT_OK_AND_ASSIGN(const TextureHandle handle_before, manager_->GetTextureHandle(id));

  const std::vector<uint8_t> replacement(4 * 4 * 4, 0xDD);
  ASSERT_OK(manager_->ReplaceTexturePixels(id, 4, 4, replacement));

  ASSERT_OK_AND_ASSIGN(Texture * texture_after, manager_->GetTexture(id));
  ASSERT_OK_AND_ASSIGN(const TextureHandle handle_after, manager_->GetTextureHandle(id));
  EXPECT_EQ(texture_after->id, definition_before.id);
  EXPECT_EQ(texture_after->path, definition_before.path);
  EXPECT_NE(handle_after, handle_before);
  EXPECT_EQ(resources_->unloaded_ids, std::vector<uint64_t>{handle_before.id()});
  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/textures/generated.png.replacement.png"));
}

TEST_F(TextureManagerTest, ShowTexturePixelsSwapsTheHandleAndLeavesTheFileAlone) {
  // The split that lets a derived terrain grow its atlas mid-edit. A cell the
  // level references must be on screen immediately, but nothing is durable
  // until the level is saved, so abandoning an edit leaves no artwork behind
  // that nothing references.
  const std::vector<uint8_t> original(4 * 4 * 4, 0x22);
  ASSERT_OK_AND_ASSIGN(const std::string id,
                       manager_->CreateTextureFromPixels("generated", 4, 4, original));
  ASSERT_OK_AND_ASSIGN(const TextureHandle handle_before, manager_->GetTextureHandle(id));
  const std::string image_path = test_dir_ + "/textures/generated.png";
  const auto written_at = std::filesystem::last_write_time(image_path);

  const std::vector<uint8_t> grown(4 * 4 * 4, 0xDD);
  ASSERT_OK(manager_->ShowTexturePixels(id, 4, 4, grown));

  ASSERT_OK_AND_ASSIGN(const TextureHandle handle_after, manager_->GetTextureHandle(id));
  EXPECT_NE(handle_after, handle_before) << "the viewport must sample the new artwork";
  EXPECT_EQ(resources_->unloaded_ids, std::vector<uint64_t>{handle_before.id()});
  EXPECT_EQ(std::filesystem::last_write_time(image_path), written_at)
      << "showing artwork must not touch the durable file";
  ASSERT_EQ(resources_->loaded_pixel_sizes.size(), 1);
  EXPECT_EQ(resources_->loaded_pixel_sizes.front(), std::make_pair(4, 4));
}

TEST_F(TextureManagerTest, ShowTexturePixelsRejectsAnUnknownTexture) {
  const std::vector<uint8_t> pixels(4 * 4 * 4, 0xAB);

  EXPECT_FALSE(manager_->ShowTexturePixels("missing", 4, 4, pixels).ok());
}

TEST_F(TextureManagerTest, CreateTextureFromPixelsRejectsBadInput) {
  const std::vector<uint8_t> pixels(4 * 4 * 4, 0xAB);
  EXPECT_FALSE(manager_->CreateTextureFromPixels("", 4, 4, pixels).ok());
  EXPECT_FALSE(manager_->CreateTextureFromPixels("wrong_size", 8, 8, pixels).ok());
}

TEST_F(TextureManagerTest, CreateTextureWithCopy) {
  // Create a dummy image file outside the test assets directory
  std::string external_img_path = test_dir_ + "/external_image.png";
  {
    std::ofstream f(external_img_path);
    f << "dummy data";
  }

  // Path is absolute (or relative to cwd, but outside assets)
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTexture({.path = external_img_path}));

  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture(id));

  // Verify path is relative to assets
  EXPECT_EQ(tex->path, "textures/external_image.png");

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
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTexture(
                                           {.path = std::filesystem::absolute(img_path).string()}));

  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture(id));

  Texture new_data = *tex;
  new_data.name = std::string(kMaxTextureNameLength + 1, 'a');

  auto status = manager_->UpdateTexture(new_data);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TextureManagerTest, RenameTexture) {
  std::string img_path = test_dir_ + "/textures/rename.png";
  {
    std::ofstream f(img_path);
    f << "dummy";
  }

  ASSERT_OK_AND_ASSIGN(
      std::string id, manager_->CreateTexture({.path = std::filesystem::absolute(img_path).string(),
                                               .name = "OldName"}));

  std::string old_file = test_dir_ + "/definitions/textures/OldName-" + id + ".json";
  ASSERT_TRUE(std::filesystem::exists(old_file));

  // Rename
  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture(id));

  Texture new_tex = *tex;
  new_tex.name = "NewName";
  ASSERT_OK(manager_->UpdateTexture(new_tex));

  std::string new_file = test_dir_ + "/definitions/textures/NewName-" + id + ".json";
  EXPECT_TRUE(std::filesystem::exists(new_file));
  EXPECT_FALSE(std::filesystem::exists(old_file));
}

TEST_F(TextureManagerTest, SaveTextureWithEmptyIdFails) {
  Texture texture{.name = "some-name", .path = "textures/some.png"};
  // id is empty — SaveTexture must reject it immediately.
  absl::Status status = TextureManagerTestPeer::SaveTexture(*manager_, texture);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TextureManagerTest, CreateTextureIdIsNonEmpty) {
  std::string img_path = test_dir_ + "/textures/nonempty_id.png";
  {
    std::ofstream f(img_path);
    f << "dummy";
  }

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTexture(
                                           {.path = std::filesystem::absolute(img_path).string()}));
  EXPECT_FALSE(id.empty());

  // The texture cached in the manager must carry the same non-empty id.
  ASSERT_OK_AND_ASSIGN(Texture * tex, manager_->GetTexture(id));
  EXPECT_EQ(tex->id, id);
}

}  // namespace
}  // namespace zebes
