#include "resources/blueprint_manager.h"

#include <gtest/gtest.h>

#include <filesystem>

#include "absl/status/status.h"
#include "common/utils.h"
#include "macros.h"

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
    ASSERT_OK_AND_ASSIGN(manager_, BlueprintManager::Create(test_dir_));
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
  // blueprint.id = "test-blueprint"; // ID is now generated
  blueprint.name = "MyBlueprint";
  blueprint.states = {{.name = "idle", .collider_id = "idle-collider"},
                      {.name = "run", .sprite_id = "run-sprite"},
                      {.name = "jump", .collider_id = "jump-collider"}};

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateBlueprint(blueprint));
  // CHECK ID
  EXPECT_FALSE(id.empty());
  EXPECT_NE(id, "test-blueprint");

  ASSERT_OK_AND_ASSIGN(Blueprint * loaded_blueprint, manager_->GetBlueprint(id));
  EXPECT_EQ(loaded_blueprint->id, id);
  ASSERT_EQ(loaded_blueprint->states.size(), 3);

  EXPECT_EQ(loaded_blueprint->states[0].name, "idle");
  EXPECT_EQ(loaded_blueprint->states[0].collider_id, "idle-collider");
  EXPECT_EQ(loaded_blueprint->states[1].name, "run");
  EXPECT_EQ(loaded_blueprint->states[1].sprite_id, "run-sprite");

  // Verify file exists
  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/blueprints/" + blueprint.name +
                                      "-" + id + ".json"));
}

// Editors hold a Blueprint* for as long as they are editing it. Replacing the
// cached unique_ptr on save freed what they held.
TEST_F(BlueprintManagerTest, SavingKeepsPointersHandedOutBeforeIt) {
  Blueprint blueprint;
  blueprint.name = "Stable";
  blueprint.states = {{.name = "idle"}};
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateBlueprint(blueprint));
  ASSERT_OK_AND_ASSIGN(Blueprint * held, manager_->GetBlueprint(id));

  Blueprint edited = *held;
  edited.name = "Renamed";
  ASSERT_OK(manager_->SaveBlueprint(edited));

  ASSERT_OK_AND_ASSIGN(Blueprint * after, manager_->GetBlueprint(id));
  EXPECT_EQ(after, held) << "the address a caller is still holding must survive a save";
  EXPECT_EQ(held->name, "Renamed");
}

TEST_F(BlueprintManagerTest, SaveBlueprintFileFormat) {
  Blueprint blueprint;
  blueprint.id = "file-format-test";
  blueprint.name = "FileFormatTest";
  blueprint.states = {{.name = "idle"}};

  ASSERT_OK(manager_->SaveBlueprint(blueprint));

  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/blueprints/" + blueprint.name +
                                      "-" + blueprint.id + ".json"));
}

TEST_F(BlueprintManagerTest, ValidationLogic) {
  Blueprint blueprint;
  blueprint.id = "validation-test";
  blueprint.name = "ValidationTest";
  blueprint.states = {{.name = ""}};  // Empty name is invalid

  auto status = manager_->SaveBlueprint(blueprint);
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsInvalidArgument(status));

  // Correction
  blueprint.states[0].name = "valid";
  EXPECT_OK(manager_->SaveBlueprint(blueprint));
}

TEST_F(BlueprintManagerTest, DeleteBlueprint) {
  Blueprint blueprint;
  // blueprint.id = "delete-test"; // ID is generated
  blueprint.name = "DeleteTest";

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateBlueprint(blueprint));

  ASSERT_OK(manager_->DeleteBlueprint(id));

  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/blueprints/" + blueprint.name +
                                       "-" + id + ".json"));
  EXPECT_FALSE(manager_->GetBlueprint(id).ok());
}

TEST_F(BlueprintManagerTest, RenameBlueprint) {
  Blueprint blueprint;
  // blueprint.id = "rename-test"; // ID is generated
  blueprint.name = "OldName";

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateBlueprint(blueprint));

  std::string old_file = test_dir_ + "/definitions/blueprints/OldName-" + id + ".json";
  ASSERT_TRUE(std::filesystem::exists(old_file));

  // Update blueprint object with generated ID for renaming
  blueprint.id = id;

  // Rename
  blueprint.name = "NewName";
  ASSERT_OK(manager_->SaveBlueprint(blueprint));

  std::string new_file = test_dir_ + "/definitions/blueprints/NewName-" + id + ".json";
  EXPECT_TRUE(std::filesystem::exists(new_file));
  EXPECT_FALSE(std::filesystem::exists(old_file));
}

}  // namespace
}  // namespace zebes
