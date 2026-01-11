#include "objects/level.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "objects/entity.h"

namespace zebes {
namespace {

TEST(LevelTest, Copy) {
  Level original_level;
  original_level.id = "level1";
  original_level.name = "Test Level";
  original_level.camera.position = {100, 200};

  // Add an entity
  Entity entity;
  entity.id = 123;
  entity.transform.position = {10, 20};
  original_level.AddEntity(std::move(entity));

  // Create copy
  Level copied_level = original_level;

  // Verify basic fields
  EXPECT_EQ(copied_level.id, "level1");
  EXPECT_EQ(copied_level.name, "Test Level");
  EXPECT_EQ(copied_level.camera.position.x, 100);
  EXPECT_EQ(copied_level.camera.position.y, 200);

  // Verify entities
  ASSERT_EQ(copied_level.entities.size(), 1);
  // Ensure the entities are different instances (deep copy via map value)
  EXPECT_NE(&copied_level.entities[123], &original_level.entities[123]);
  EXPECT_EQ(copied_level.entities[123].id, 123);
  EXPECT_EQ(copied_level.entities[123].transform.position.x, 10);

  // Modify copy entity and ensure original is unchanged
  copied_level.entities[123].transform.position.x = 999;
  EXPECT_EQ(original_level.entities[123].transform.position.x, 10);
}

}  // namespace
}  // namespace zebes
