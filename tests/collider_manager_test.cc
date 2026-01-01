#include "resources/collider_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "common/utils.h"

namespace zebes {
namespace {

class ColliderManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "test_collider_assets_" + GenerateGuid();
    std::filesystem::remove_all(test_dir_);  // Ensure clean

    // Setup structure for ColliderManager
    std::filesystem::create_directories(test_dir_ + "/definitions/colliders");

    // Create manager
    auto cm = ColliderManager::Create(test_dir_);
    ASSERT_TRUE(cm.ok());
    manager_ = std::move(*cm);
  }

  void TearDown() override {
    manager_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  std::string test_dir_;
  std::unique_ptr<ColliderManager> manager_;
};

TEST_F(ColliderManagerTest, CreateAndGetCollider) {
  Collider collider;
  collider.name = "TestCollider";

  Polygon poly1;
  poly1.push_back({0, 0});
  poly1.push_back({10, 0});
  poly1.push_back({0, 10});
  collider.polygons.push_back(poly1);

  auto id_or = manager_->CreateCollider(collider);
  ASSERT_TRUE(id_or.ok()) << id_or.status();
  std::string id = *id_or;
  EXPECT_FALSE(id.empty());
  EXPECT_NE(id, "test-collider");  // Should be generated

  auto collider_or = manager_->GetCollider(id);
  ASSERT_TRUE(collider_or.ok());
  Collider* loaded_collider = *collider_or;
  EXPECT_EQ(loaded_collider->id, id);
  ASSERT_EQ(loaded_collider->polygons.size(), 1);
  EXPECT_EQ(loaded_collider->polygons[0].size(), 3);

  // Verify file exists
  EXPECT_TRUE(
      std::filesystem::exists(test_dir_ + "/definitions/colliders/TestCollider-" + id + ".json"));
}

TEST_F(ColliderManagerTest, LoadAllColliders) {
  // Manually create a JSON file
  std::string id = "manual-collider";
  std::string json_content = R"({
    "id": "manual-collider",
    "name": "ManualCollider",
    "polygons": [
       [ {"x": 1, "y": 1}, {"x": 2, "y": 2} ]
    ]
  })";

  {
    std::ofstream f(test_dir_ + "/definitions/colliders/ManualCollider-" + id + ".json");
    f << json_content;
  }

  // Load
  auto status = manager_->LoadAllColliders();
  ASSERT_TRUE(status.ok()) << status;

  auto collider_or = manager_->GetCollider(id);
  ASSERT_TRUE(collider_or.ok());
  EXPECT_EQ((*collider_or)->id, "manual-collider");
  EXPECT_EQ((*collider_or)->polygons.size(), 1);
  EXPECT_EQ((*collider_or)->polygons[0][0].x, 1);
}

TEST_F(ColliderManagerTest, UpdateCollider) {
  Collider collider;
  // collider.id = "update-test"; // ID is generated
  collider.name = "UpdateTest";

  auto id_or = manager_->CreateCollider(collider);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  // Update
  collider = *(*manager_->GetCollider(id));
  Polygon p;
  p.push_back({5, 5});
  collider.polygons.push_back(p);

  auto status = manager_->SaveCollider(collider);
  ASSERT_TRUE(status.ok());

  // Check
  auto collider_or = manager_->GetCollider(id);
  EXPECT_EQ((*collider_or)->polygons.size(), 1);
  EXPECT_EQ((*collider_or)->polygons[0][0].x, 5);
}

TEST_F(ColliderManagerTest, DeleteCollider) {
  Collider collider;
  // collider.id = "delete-test"; // ID is generated
  collider.name = "DeleteTest";

  auto id_or = manager_->CreateCollider(collider);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  auto status = manager_->DeleteCollider(id);
  ASSERT_TRUE(status.ok());

  EXPECT_FALSE(
      std::filesystem::exists(test_dir_ + "/definitions/colliders/DeleteTest-" + id + ".json"));
  EXPECT_FALSE(manager_->GetCollider(id).ok());
}

TEST_F(ColliderManagerTest, LoadInvalidJsonMissingName) {
  // Manually create a JSON file without name
  std::string id = "invalid-collider";
  std::string json_content = R"({
    "id": "invalid-collider",
    "polygons": []
  })";

  {
    std::ofstream f(test_dir_ + "/definitions/colliders/" + id + ".json");
    f << json_content;
  }

  // Load should fail or log warning. LoadAllColliders swallows errors but logs warnings.
  // Let's try LoadCollider directly to verify it returns error.
  auto status = manager_->LoadCollider(id + ".json");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInternal);
  EXPECT_TRUE(absl::StrContains(status.status().message(), "JSON parsing error"));
}

TEST_F(ColliderManagerTest, RenameCollider) {
  Collider collider;
  // collider.id = "rename-test";  // Will be ignored
  collider.name = "OldName";

  auto id_or = manager_->CreateCollider(collider);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  std::string old_file = test_dir_ + "/definitions/colliders/OldName-" + id + ".json";
  ASSERT_TRUE(std::filesystem::exists(old_file));

  // Rename
  // We need to fetch it first to make sure we have the correct object state or just update the one
  // we have with the new ID
  collider.id = id;
  collider.name = "NewName";
  ASSERT_TRUE(manager_->SaveCollider(collider).ok());

  std::string new_file = test_dir_ + "/definitions/colliders/NewName-" + id + ".json";
  EXPECT_TRUE(std::filesystem::exists(new_file));
  EXPECT_FALSE(std::filesystem::exists(old_file));
}

}  // namespace
}  // namespace zebes
