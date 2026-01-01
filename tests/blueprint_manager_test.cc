#include "resources/blueprint_manager.h"

#include <gtest/gtest.h>

#include <filesystem>

#include "absl/status/status.h"
#include "common/utils.h"

namespace zebes {
namespace {

class BlueprintManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "test_blueprint_assets_" + GenerateGuid();
    std::filesystem::remove_all(test_dir_);  // Ensure clean

    // Setup structure for BlueprintManager
    std::filesystem::create_directories(test_dir_ + "/definitions/blueprints");

    // Create manager
    auto bm = BlueprintManager::Create(test_dir_);
    ASSERT_TRUE(bm.ok());
    manager_ = std::move(*bm);
  }

  void TearDown() override {
    manager_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  std::string test_dir_;
  std::unique_ptr<BlueprintManager> manager_;
};

TEST_F(BlueprintManagerTest, CreateAndGetBlueprint) {
  Blueprint blueprint;
  blueprint.id = "test-blueprint";
  blueprint.name = "MyBlueprint";
  blueprint.states = {"idle", "run", "jump"};

  // Valid indices
  blueprint.collider_ids[0] = "idle-collider";
  blueprint.collider_ids[2] = "jump-collider";
  blueprint.sprite_ids[1] = "run-sprite";

  auto id_or = manager_->CreateBlueprint(blueprint);
  ASSERT_TRUE(id_or.ok()) << id_or.status();
  std::string id = *id_or;
  EXPECT_EQ(id, "test-blueprint");

  auto blueprint_or = manager_->GetBlueprint(id);
  ASSERT_TRUE(blueprint_or.ok());
  Blueprint* loaded_blueprint = *blueprint_or;
  EXPECT_EQ(loaded_blueprint->id, id);
  ASSERT_EQ(loaded_blueprint->states.size(), 3);
  EXPECT_EQ(loaded_blueprint->collider_ids.size(), 2);
  EXPECT_EQ(loaded_blueprint->sprite_ids.size(), 1);
  EXPECT_EQ(loaded_blueprint->collider_ids[0], "idle-collider");

  // Verify file exists
  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/blueprints/" + blueprint.name +
                                      "-" + id + ".json"));
}

TEST_F(BlueprintManagerTest, SaveBlueprintFileFormat) {
  Blueprint blueprint;
  blueprint.id = "file-format-test";
  blueprint.name = "FileFormatTest";
  blueprint.states = {"idle"};

  auto status = manager_->SaveBlueprint(blueprint);
  ASSERT_TRUE(status.ok());

  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/blueprints/" + blueprint.name +
                                      "-" + blueprint.id + ".json"));
}

TEST_F(BlueprintManagerTest, ValidationLogic) {
  Blueprint blueprint;
  blueprint.id = "validation-test";
  blueprint.name = "ValidationTest";
  blueprint.states = {"state1", "state2"};  // Size is 2, valid indices 0, 1

  // Invalid index (>= size)
  blueprint.collider_ids[2] = "invalid-collider";

  auto status = manager_->SaveBlueprint(blueprint);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsInvalidArgument(status));

  // Invalid index (negative)
  blueprint.collider_ids.clear();
  blueprint.collider_ids[-1] = "invalid-collider";
  status = manager_->SaveBlueprint(blueprint);
  EXPECT_FALSE(status.ok());

  // Correction
  blueprint.collider_ids.clear();
  blueprint.collider_ids[1] = "valid-collider";
  status = manager_->SaveBlueprint(blueprint);
  EXPECT_TRUE(status.ok());
}

TEST_F(BlueprintManagerTest, SpriteValidationLogic) {
  Blueprint blueprint;
  blueprint.id = "sprite-validation-test";
  blueprint.name = "SpriteValidationTest";
  blueprint.states = {"state1"};  // Size 1, valid index 0

  // Invalid index
  blueprint.sprite_ids[1] = "invalid-sprite";

  auto status = manager_->SaveBlueprint(blueprint);
  EXPECT_FALSE(status.ok());

  // Valid
  blueprint.sprite_ids.clear();
  blueprint.sprite_ids[0] = "valid-sprite";
  status = manager_->SaveBlueprint(blueprint);
  EXPECT_TRUE(status.ok());
}

TEST_F(BlueprintManagerTest, DeleteBlueprint) {
  Blueprint blueprint;
  blueprint.id = "delete-test";
  blueprint.name = "DeleteTest";

  auto id_or = manager_->CreateBlueprint(blueprint);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  auto status = manager_->DeleteBlueprint(id);
  ASSERT_TRUE(status.ok());

  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/blueprints/" + id + ".json"));
  EXPECT_FALSE(manager_->GetBlueprint(id).ok());
}

TEST_F(BlueprintManagerTest, RenameBlueprint) {
  Blueprint blueprint;
  blueprint.id = "rename-test";
  blueprint.name = "OldName";

  ASSERT_TRUE(manager_->CreateBlueprint(blueprint).ok());

  std::string old_file = test_dir_ + "/definitions/blueprints/OldName-rename-test.json";
  ASSERT_TRUE(std::filesystem::exists(old_file));

  // Rename
  blueprint.name = "NewName";
  ASSERT_TRUE(manager_->SaveBlueprint(blueprint).ok());

  std::string new_file = test_dir_ + "/definitions/blueprints/NewName-rename-test.json";
  EXPECT_TRUE(std::filesystem::exists(new_file));
  EXPECT_FALSE(std::filesystem::exists(old_file));
}

}  // namespace
}  // namespace zebes
