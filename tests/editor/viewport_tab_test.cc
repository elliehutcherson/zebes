#include "editor/level_editor/viewport_tab.h"
#include "editor/level_editor/viewport_model.h"

#include <limits>
#include <map>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "objects/tileset.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

// Exposes private ViewportTab methods for testing.
class ViewportTabTestPeer {
 public:
  static absl::Status HandleTileInput(ViewportTab& tab, Level& level, const Tile& tile,
                                      Vec mouse_world, int tile_render_w, int tile_render_h) {
    return tab.HandleTileInput(level, tile, mouse_world, tile_render_w, tile_render_h);
  }

  static void ApplyPendingCameraFrame(ViewportTab& tab, ImVec2 viewport_size,
                                      VisibleWorldBounds world_bounds) {
    tab.ApplyPendingCameraFrame(viewport_size, world_bounds);
  }

  static const Camera& GetCamera(const ViewportTab& tab) { return tab.camera_; }

  static void SetParallaxPreviewMode(ViewportTab& tab, ParallaxPreviewMode mode) {
    tab.parallax_preview_mode_ = mode;
  }

  static ParallaxPreviewMode GetParallaxPreviewMode(const ViewportTab& tab) {
    return tab.parallax_preview_mode_;
  }

  static void ReconcileParallaxPreviewMode(ViewportTab& tab,
                                           const ViewportRenderOptions& options) {
    tab.ReconcileParallaxPreviewMode(options);
  }
};

namespace {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

// PickEntity tests

TEST(PickEntityTest, HitEntityWithNoSprite) {
  Entity e = {.id = 1, .active = true, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Center of the default 32x32 box
  EXPECT_EQ(PickEntity(entities, {100, 200}).value(), 1u);
}

TEST(PickEntityTest, MissReturnsFallbackId) {
  Entity e = {.id = 1, .active = true, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Well outside the 32x32 hit box
  EXPECT_EQ(PickEntity(entities, {500, 500}).value(), Entity::kInvalidId);
}

TEST(PickEntityTest, InactiveEntityIsSkipped) {
  Entity e = {.id = 1, .active = false, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  EXPECT_EQ(PickEntity(entities, {100, 200}).value(), Entity::kInvalidId);
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

  EXPECT_EQ(PickEntity(entities, {55, 95}).value(), 7u);
  EXPECT_EQ(PickEntity(entities, {35, 95}).value(), Entity::kInvalidId);
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

TEST(ViewportTabTest, FrameZoneCentersAndFitsStableZoneBounds) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);
  ParallaxZone zone{
      .id = 42,
      .min_point = {100, 200},
      .max_point = {500, 400},
  };

  tab.FrameZone(zone);
  ViewportTabTestPeer::ApplyPendingCameraFrame(
      tab, ImVec2(1000, 800), {.min = {0, 0}, .max = {1000, 1000}});

  const Camera& camera = ViewportTabTestPeer::GetCamera(tab);
  EXPECT_DOUBLE_EQ(camera.position.x, 300);
  EXPECT_DOUBLE_EQ(camera.position.y, 300);
  EXPECT_DOUBLE_EQ(camera.zoom, 2);
}

TEST(ViewportTabTest, RenderRejectsMissingLevelBeforeOpeningCanvas) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);

  EXPECT_EQ(tab.Render({}).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ViewportTabTest, RenderRejectsInvalidLevelGeometryBeforeOpeningCanvas) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);
  Level level{
      .tile_render_width = 0,
      .width = std::numeric_limits<double>::infinity(),
  };

  EXPECT_EQ(tab.Render({.level = &level}).code(), absl::StatusCode::kInvalidArgument);

  level.width = 0;
  EXPECT_EQ(tab.Render({.level = &level}).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ViewportTabTest, SelectedParallaxPreviewRequiresCompatibleSelection) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);

  ViewportTabTestPeer::SetParallaxPreviewMode(tab,
                                              ParallaxPreviewMode::kSelectedLayer);
  ViewportTabTestPeer::ReconcileParallaxPreviewMode(
      tab, {.selected_parallax_theme_id = 3,
            .selected_parallax_layer_index = 1});
  EXPECT_EQ(ViewportTabTestPeer::GetParallaxPreviewMode(tab),
            ParallaxPreviewMode::kSelectedLayer);

  ViewportTabTestPeer::ReconcileParallaxPreviewMode(
      tab, {.selected_parallax_theme_id = 3});
  EXPECT_EQ(ViewportTabTestPeer::GetParallaxPreviewMode(tab),
            ParallaxPreviewMode::kActiveZone);

  ViewportTabTestPeer::SetParallaxPreviewMode(tab,
                                              ParallaxPreviewMode::kSelectedTheme);
  ViewportTabTestPeer::ReconcileParallaxPreviewMode(tab, {});
  EXPECT_EQ(ViewportTabTestPeer::GetParallaxPreviewMode(tab),
            ParallaxPreviewMode::kActiveZone);
}

// SnapEntityToGrid tests

TEST(SnapEntityToGridTest, ColliderCenterAlignedAndBottomAligned) {
  // Tile grid: 16×16. Mouse at (20, 20) → tile (1, 1): center_x=24, bottom_y=32.
  // Collider: a box from x∈[-8,8], y∈[-16,0] → center_x_offset=0, max_y=0.
  Collider collider;
  collider.polygons.push_back({{-8, -16}, {8, -16}, {8, 0}, {-8, 0}});
  absl::StatusOr<Vec> result = SnapEntityToGrid({20, 20}, 16, 16, &collider, nullptr);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->x, 24.0);  // cell_center_x - 0
  EXPECT_DOUBLE_EQ(result->y, 32.0);  // cell_bottom_y - 0
}

TEST(SnapEntityToGridTest, ColliderAsymmetricBoundingBox) {
  // Tile grid: 16×16. Mouse at (0, 0) → tile (0, 0): center_x=8, bottom_y=16.
  // Collider x∈[4,12] → center_x_offset=8; max_y=10.
  Collider collider;
  collider.polygons.push_back({{4, 0}, {12, 0}, {12, 10}, {4, 10}});
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, &collider, nullptr);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->x, 0.0);   // 8 - 8
  EXPECT_DOUBLE_EQ(result->y, 6.0);   // 16 - 10
}

TEST(SnapEntityToGridTest, SpriteFallbackWhenNoCollider) {
  // Tile grid: 16×16. Mouse at (0, 0) → tile (0, 0): center_x=8, bottom_y=16.
  // Sprite: render_w=48, render_h=64, offset_x=-24, offset_y=0.
  // center_x_offset = -24 + 24 = 0; bottom_y_offset = 0 + 64 = 64.
  Sprite sprite;
  sprite.frames.push_back(
      SpriteFrame{.render_w = 48, .render_h = 64, .offset_x = -24, .offset_y = 0});
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, nullptr, &sprite);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->x, 8.0);    // 8 - 0
  EXPECT_DOUBLE_EQ(result->y, -48.0);  // 16 - 64
}

TEST(SnapEntityToGridTest, ColliderTakesPriorityOverSprite) {
  // Both collider and sprite present; collider wins.
  // Tile grid: 16×16. Mouse at (0, 0) → center_x=8, bottom_y=16.
  // Collider x∈[0,16], y∈[0,16] → center=8, bottom=16 → pos=(0,0).
  Collider collider;
  collider.polygons.push_back({{0, 0}, {16, 0}, {16, 16}, {0, 16}});
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 48, .render_h = 64});
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, &collider, &sprite);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->x, 0.0);
  EXPECT_DOUBLE_EQ(result->y, 0.0);
}

TEST(SnapEntityToGridTest, ErrorWhenNeitherColliderNorSprite) {
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, nullptr, nullptr);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(SnapEntityToGridTest, ErrorWhenSpriteFrameHasZeroDimensions) {
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 0, .render_h = 0});
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, nullptr, &sprite);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(SnapEntityToGridTest, ErrorWhenTileDimensionsAreInvalid) {
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 16, .render_h = 16});

  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 0, 16, nullptr, &sprite);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(SnapEntityToGridTest, RejectsUnrepresentableGridCoordinate) {
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 16, .render_h = 16});

  absl::StatusOr<Vec> result = SnapEntityToGrid(
      {std::numeric_limits<double>::max(), 0}, 16, 16, nullptr, &sprite);

  EXPECT_EQ(result.status().code(), absl::StatusCode::kOutOfRange);
}

TEST(SnapEntityToGridTest, EmptyColliderFallsBackToSprite) {
  // Collider present but has no polygons — falls through to sprite.
  Collider collider;  // polygons empty
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 16, .render_h = 16, .offset_x = 0, .offset_y = 0});
  // Tile grid: 16×16. Mouse at (0, 0) → center_x=8, bottom_y=16.
  // Sprite: center_x_offset=8, bottom_y_offset=16 → pos=(0, 0).
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, &collider, &sprite);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_DOUBLE_EQ(result->x, 0.0);
  EXPECT_DOUBLE_EQ(result->y, 0.0);
}

// PickEntity already uses a 32x32 default hit-box for entities with no sprite.
// Verify both edges of that box for an entity created from an invisible blueprint.
TEST(PickEntityTest, InvisibleBlueprintEntity_HitsDefaultBox) {
  // Simulates an entity placed from an invisible blueprint: sprite is null.
  Entity e = {.id = 3, .active = true, .transform = {.position = {50, 80}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Hits are inside the default ±16 box around (50, 80): x∈[34,66], y∈[64,96].
  EXPECT_EQ(PickEntity(entities, {50, 80}).value(), 3u);  // center
  EXPECT_EQ(PickEntity(entities, {34, 64}).value(), 3u);  // top-left corner
  EXPECT_EQ(PickEntity(entities, {66, 96}).value(), 3u);  // bottom-right corner

  // Miss just outside the box.
  EXPECT_EQ(PickEntity(entities, {33, 80}).value(), Entity::kInvalidId);
  EXPECT_EQ(PickEntity(entities, {67, 80}).value(), Entity::kInvalidId);
}

TEST(PickEntityTest, InvalidSpriteBoundsFailFast) {
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 0, .render_h = 16});
  Entity entity{.id = 1, .sprite = &sprite};
  std::map<uint64_t, Entity> entities{{entity.id, entity}};

  EXPECT_EQ(PickEntity(entities, {0, 0}).status().code(), absl::StatusCode::kInvalidArgument);
}

// HandleTileInput tests
//
// These tests exercise the left-paint / right-erase logic in HandleTileInput.
// The key invariant: painting must only occur when the LEFT mouse button is
// held. The canvas InvisibleButton is registered for all three mouse buttons,
// so IsItemActive() returns true on right-click too — the paint guard must
// additionally check MouseDown[Left] to avoid painting on right-click.

class HandleTileInputTest : public ::testing::Test {
 protected:
  void SetUp() override {
    io_.MouseDown[ImGuiMouseButton_Left] = false;
    io_.MouseDown[ImGuiMouseButton_Right] = false;
    ON_CALL(gui_, GetIO()).WillByDefault(ReturnRef(io_));
    ON_CALL(gui_, IsItemActive()).WillByDefault(Return(false));
    ON_CALL(gui_, IsItemClicked(ImGuiMouseButton_Right)).WillByDefault(Return(false));
  }

  NiceMock<MockApi> api_;
  NiceMock<MockGui> gui_;
  ImGuiIO io_{};
  ViewportTab tab_{api_, &gui_};

  Tile tile_{.id = 7, .source_x = 0, .source_y = 0};

  // Mouse positioned at world (8, 8) with a 16×16 render grid → tile (0, 0).
  static constexpr Vec kMouseWorld = {8.0, 8.0};
  static constexpr int kTileRenderW = 16;
  static constexpr int kTileRenderH = 16;
};

TEST_F(HandleTileInputTest, LeftHeld_PaintsTile) {
  Level level{.width = 100, .height = 100};
  ON_CALL(gui_, IsItemActive()).WillByDefault(Return(true));
  io_.MouseDown[ImGuiMouseButton_Left] = true;

  ASSERT_TRUE(
      ViewportTabTestPeer::HandleTileInput(tab_, level, tile_, kMouseWorld, kTileRenderW,
                                           kTileRenderH).ok());

  EXPECT_EQ(GetTileAt(level, 0, 0).value(), tile_.id);
}

TEST_F(HandleTileInputTest, RightClick_ErasesToile) {
  Level level{.width = 100, .height = 100};
  // Pre-place a tile so we can verify it gets cleared.
  ASSERT_TRUE(SetTileAt(level, 0, 0, tile_.id).ok());
  ON_CALL(gui_, IsItemClicked(ImGuiMouseButton_Right)).WillByDefault(Return(true));

  ASSERT_TRUE(
      ViewportTabTestPeer::HandleTileInput(tab_, level, tile_, kMouseWorld, kTileRenderW,
                                           kTileRenderH).ok());

  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 0);
}

// Regression: the canvas InvisibleButton captures right-click, making
// IsItemActive() true on right-click. Without checking MouseDown[Left], the
// paint branch fires on right-click instead of the erase branch.
TEST_F(HandleTileInputTest, RightClickActivatesItem_ErasesNotPaints) {
  Level level{.width = 100, .height = 100};
  ASSERT_TRUE(SetTileAt(level, 0, 0, tile_.id).ok());

  // Simulate right-click activating the canvas item (the bug condition):
  // IsItemActive() is true, but the left mouse button is NOT held.
  ON_CALL(gui_, IsItemActive()).WillByDefault(Return(true));
  io_.MouseDown[ImGuiMouseButton_Left] = false;
  ON_CALL(gui_, IsItemClicked(ImGuiMouseButton_Right)).WillByDefault(Return(true));

  ASSERT_TRUE(
      ViewportTabTestPeer::HandleTileInput(tab_, level, tile_, kMouseWorld, kTileRenderW,
                                           kTileRenderH).ok());

  EXPECT_EQ(GetTileAt(level, 0, 0).value(), 0) << "Right-click must erase, not paint";
}

TEST_F(HandleTileInputTest, MouseOutsideLevelDoesNotMutateTiles) {
  Level level{.width = 100, .height = 100};
  ON_CALL(gui_, IsItemActive()).WillByDefault(Return(true));
  io_.MouseDown[ImGuiMouseButton_Left] = true;

  ASSERT_TRUE(ViewportTabTestPeer::HandleTileInput(tab_, level, tile_, {-1, 8}, kTileRenderW,
                                                   kTileRenderH).ok());
  EXPECT_TRUE(level.tile_chunks.empty());
}

TEST_F(HandleTileInputTest, UnavailableMouseSentinelDoesNotAttemptGridConversion) {
  Level level{.width = 100, .height = 100};
  ON_CALL(gui_, IsItemActive()).WillByDefault(Return(true));
  io_.MouseDown[ImGuiMouseButton_Left] = true;

  const double unavailable = -std::numeric_limits<float>::max();
  ASSERT_TRUE(ViewportTabTestPeer::HandleTileInput(
                  tab_, level, tile_, {unavailable, unavailable}, kTileRenderW,
                  kTileRenderH)
                  .ok());
  EXPECT_TRUE(level.tile_chunks.empty());
}

TEST(TileMutationTest, RejectsNegativeCoordinates) {
  Level level;

  EXPECT_EQ(SetTileAt(level, -1, 0, 1).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(GetTileAt(level, 0, -1).status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(level.tile_chunks.empty());
}

TEST(TileChunkKeyTest, RoundTripsSignedCoordinatesWithoutUndefinedShifts) {
  const TileChunkCoordinate coordinate = DecodeChunkKey(ChunkKey(-2, 3));

  EXPECT_EQ(coordinate.x, -2);
  EXPECT_EQ(coordinate.y, 3);
}

}  // namespace
}  // namespace zebes
