#include "editor/level_editor/viewport_tab.h"

#include <map>

#include "gtest/gtest.h"
#include "objects/entity.h"
#include "objects/sprite.h"

namespace zebes {
namespace {

TEST(CalculateParallaxOffsetTest, ZeroCameraPos) {
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(0.0, 0.5, 2.0), 0.0);
}

TEST(CalculateParallaxOffsetTest, FullScrollFactor) {
  // scroll_factor=1.0 means layer moves fully with camera
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(100.0, 1.0, 1.0), 100.0);
}

TEST(CalculateParallaxOffsetTest, ZeroScrollFactor) {
  // scroll_factor=0.0 means layer is fixed (distant background)
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(100.0, 0.0, 1.0), 0.0);
}

TEST(CalculateParallaxOffsetTest, HalfScrollFactor) {
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(200.0, 0.5, 1.0), 100.0);
}

TEST(CalculateParallaxOffsetTest, ZoomScales) {
  EXPECT_DOUBLE_EQ(CalculateParallaxOffset(100.0, 1.0, 2.0), 200.0);
}

// PickEntity tests

TEST(PickEntityTest, HitEntityWithNoSprite) {
  Entity e = {.id = 1, .active = true, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Center of the default 32x32 box
  EXPECT_EQ(PickEntity(entities, {100, 200}), 1u);
}

TEST(PickEntityTest, MissReturnsFallbackId) {
  Entity e = {.id = 1, .active = true, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Well outside the 32x32 hit box
  EXPECT_EQ(PickEntity(entities, {500, 500}), Entity::kInvalidId);
}

TEST(PickEntityTest, InactiveEntityIsSkipped) {
  Entity e = {.id = 1, .active = false, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  EXPECT_EQ(PickEntity(entities, {100, 200}), Entity::kInvalidId);
}

TEST(PickEntityTest, HitEntityWithSprite) {
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{
      .render_w = 20,
      .render_h = 40,
      .offset_x = -10,
      .offset_y = -20,
  });

  // Entity at (50, 100); sprite box: x in [40, 60], y in [80, 120]
  Entity e = {.id = 7, .active = true, .transform = {.position = {50, 100}}, .sprite = &sprite};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  EXPECT_EQ(PickEntity(entities, {55, 95}), 7u);
  EXPECT_EQ(PickEntity(entities, {35, 95}), Entity::kInvalidId);
}

// CreateEntityFromBlueprint tests

TEST(CreateEntityFromBlueprintTest, SetsFields) {
  Blueprint bp{.id = "blueprint-abc", .states = {Blueprint::State{.name = "Idle"}}};

  Entity e = CreateEntityFromBlueprint(bp, /*state_index=*/0, {256, 512}, /*id=*/42);

  EXPECT_EQ(e.id, 42u);
  EXPECT_EQ(e.blueprint_id, "blueprint-abc");
  EXPECT_EQ(e.blueprint_state_index, 0);
  EXPECT_EQ(e.transform.position.x, 256);
  EXPECT_EQ(e.transform.position.y, 512);
  EXPECT_EQ(e.sprite, nullptr);
  EXPECT_EQ(e.collider, nullptr);
}

TEST(CreateEntityFromBlueprintTest, StateIndexPreserved) {
  Blueprint bp{
      .id = "bp-xyz",
      .states = {Blueprint::State{.name = "State0"}, Blueprint::State{.name = "State1"}},
  };

  Entity e = CreateEntityFromBlueprint(bp, /*state_index=*/1, {0, 0}, /*id=*/1);

  EXPECT_EQ(e.blueprint_state_index, 1);
}

// Invisible blueprint tests — a blueprint with no sprite_id in its state.
// Regression: RenderPlacementGhost used to read an uninitialized Sprite* pointer
// when the blueprint had no sprite, causing a segfault on selection.

TEST(CreateEntityFromBlueprintTest, InvisibleBlueprint_SpriteRemainsNull) {
  // A blueprint with a state but no sprite_id is "invisible".
  Blueprint bp{.id = "invisible-bp", .states = {Blueprint::State{.name = "Idle"}}};
  ASSERT_FALSE(bp.sprite_id(0).has_value()) << "Precondition: blueprint has no sprite";

  Entity e = CreateEntityFromBlueprint(bp, /*state_index=*/0, {100, 200}, /*id=*/5);

  EXPECT_EQ(e.id, 5u);
  EXPECT_EQ(e.blueprint_id, "invisible-bp");
  EXPECT_EQ(e.sprite, nullptr);
}

TEST(CreateEntityFromBlueprintTest, InvisibleBlueprint_NoStates_SpriteRemainsNull) {
  // A blueprint with no states at all also has no sprite.
  Blueprint bp{.id = "empty-bp"};
  ASSERT_FALSE(bp.sprite_id(0).has_value()) << "Precondition: blueprint has no states";

  Entity e = CreateEntityFromBlueprint(bp, /*state_index=*/0, {0, 0}, /*id=*/1);

  EXPECT_EQ(e.sprite, nullptr);
}

// NextAvailableEntityId tests

TEST(NextAvailableEntityIdTest, EmptyMapReturnsOne) {
  std::map<uint64_t, Entity> entities;
  EXPECT_EQ(NextAvailableEntityId(entities), 1u);
}

TEST(NextAvailableEntityIdTest, SingleEntity) {
  Entity e = {.id = 5};
  std::map<uint64_t, Entity> entities{{e.id, e}};
  EXPECT_EQ(NextAvailableEntityId(entities), 6u);
}

TEST(NextAvailableEntityIdTest, ReturnsOnePastMax) {
  std::map<uint64_t, Entity> entities;
  for (uint64_t id : {1u, 50u, 120u, 7u}) {
    entities[id] = Entity{.id = id};
  }
  EXPECT_EQ(NextAvailableEntityId(entities), 121u);
}

// Regression: placing entities after loading a saved level must not reuse
// existing IDs. Simulate the bug by building a map that looks like a loaded
// level (IDs 1..N), then verify NextAvailableEntityId returns N+1 so that the
// counter is advanced past all loaded IDs before the first placement.
TEST(NextAvailableEntityIdTest, NeverCollisdesWithLoadedLevel) {
  constexpr uint64_t kLoadedCount = 100;
  std::map<uint64_t, Entity> entities;
  for (uint64_t id = 1; id <= kLoadedCount; ++id) {
    entities[id] = Entity{.id = id};
  }

  uint64_t next_id = NextAvailableEntityId(entities);
  EXPECT_EQ(next_id, kLoadedCount + 1);

  // Simulating placement: every ID issued by the counter must be absent from
  // the loaded entity map.
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(entities.count(next_id), 0u) << "ID " << next_id << " collides with a loaded entity";
    ++next_id;
  }
}

// PickEntity already uses a 32x32 default hit-box for entities with no sprite.
// Verify both edges of that box for an entity created from an invisible blueprint.
TEST(PickEntityTest, InvisibleBlueprintEntity_HitsDefaultBox) {
  // Simulates an entity placed from an invisible blueprint: sprite is null.
  Entity e = {.id = 3, .active = true, .transform = {.position = {50, 80}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Hits are inside the default ±16 box around (50, 80): x∈[34,66], y∈[64,96].
  EXPECT_EQ(PickEntity(entities, {50, 80}), 3u);   // center
  EXPECT_EQ(PickEntity(entities, {34, 64}), 3u);   // top-left corner
  EXPECT_EQ(PickEntity(entities, {66, 96}), 3u);   // bottom-right corner

  // Miss just outside the box.
  EXPECT_EQ(PickEntity(entities, {33, 80}), Entity::kInvalidId);
  EXPECT_EQ(PickEntity(entities, {67, 80}), Entity::kInvalidId);
}

}  // namespace
}  // namespace zebes
