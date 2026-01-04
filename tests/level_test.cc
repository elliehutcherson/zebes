#include "objects/level.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "objects/entity.h"

namespace zebes {
namespace {

TEST(LevelTest, GetCopy) {
  Level original_level;
  original_level.id = "level1";
  original_level.name = "Test Level";
  original_level.camera.position = {100, 200};

  // Add an entity
  auto entity = std::make_unique<Entity>();
  entity->id = 123;
  entity->transform.position = {10, 20};
  original_level.AddEntity(std::move(entity));

  // Create copy
  Level copied_level = original_level.GetCopy();

  // Verify basic fields
  EXPECT_EQ(copied_level.id, "level1");
  EXPECT_EQ(copied_level.name, "Test Level");
  EXPECT_EQ(copied_level.camera.position.x, 100);
  EXPECT_EQ(copied_level.camera.position.y, 200);

  // Verify entities
  ASSERT_EQ(copied_level.entities.size(), 1);
  // Ensure the pointers are different (deep copy)
  EXPECT_NE(copied_level.entities[0].get(), original_level.entities[0].get());
  EXPECT_EQ(copied_level.entities[0]->id, 123);
  EXPECT_EQ(copied_level.entities[0]->transform.position.x, 10);

  // Verify lookup map is correctly updated
  ASSERT_EQ(copied_level.entity_lookup.size(), 1);
  EXPECT_EQ(copied_level.entity_lookup[123], copied_level.entities[0].get());

  // Modify copy entity and ensure original is unchanged
  copied_level.entities[0]->transform.position.x = 999;
  EXPECT_EQ(original_level.entities[0]->transform.position.x, 10);
}

}  // namespace
}  // namespace zebes
