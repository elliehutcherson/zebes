#include "resources/collider_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "common/utils.h"
#include "objects/vec.h"

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
  collider.id = "test-collider";

  Polygon poly1;
  poly1.push_back({0, 0});
  poly1.push_back({10, 0});
  poly1.push_back({0, 10});
  collider.polygons.push_back(poly1);

  auto id_or = manager_->CreateCollider(collider);
  ASSERT_TRUE(id_or.ok()) << id_or.status();
  std::string id = *id_or;
  EXPECT_EQ(id, "test-collider");

  auto collider_or = manager_->GetCollider(id);
  ASSERT_TRUE(collider_or.ok());
  Collider* loaded_collider = *collider_or;
  EXPECT_EQ(loaded_collider->id, id);
  ASSERT_EQ(loaded_collider->polygons.size(), 1);
  EXPECT_EQ(loaded_collider->polygons[0].size(), 3);

  // Verify file exists
  EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/definitions/colliders/" + id + ".json"));
}

TEST_F(ColliderManagerTest, LoadAllColliders) {
  // Manually create a JSON file
  std::string id = "manual-collider";
  std::string json_content = R"({
    "id": "manual-collider",
    "polygons": [
       [ {"x": 1, "y": 1}, {"x": 2, "y": 2} ]
    ]
  })";

  {
    std::ofstream f(test_dir_ + "/definitions/colliders/" + id + ".json");
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
  collider.id = "update-test";

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
  collider.id = "delete-test";

  auto id_or = manager_->CreateCollider(collider);
  ASSERT_TRUE(id_or.ok());
  std::string id = *id_or;

  auto status = manager_->DeleteCollider(id);
  ASSERT_TRUE(status.ok());

  EXPECT_FALSE(std::filesystem::exists(test_dir_ + "/definitions/colliders/" + id + ".json"));
  EXPECT_FALSE(manager_->GetCollider(id).ok());
}

}  // namespace
}  // namespace zebes
