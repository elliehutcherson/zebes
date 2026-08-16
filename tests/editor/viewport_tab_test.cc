#include "editor/level_editor/viewport_tab.h"

#include <limits>
#include <map>

#include "editor/level_editor/viewport_model.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/sprite.h"
#include "tests/api_mock.h"
#include "tests/editor/mock_gui.h"

namespace zebes {

// Exposes private ViewportTab methods for testing.
class ViewportTabTestPeer {
 public:
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

  static void ReconcileParallaxPreviewMode(ViewportTab& tab, const ViewportRenderOptions& options) {
    tab.ReconcileParallaxPreviewMode(options);
  }

  static absl::StatusOr<std::optional<ActiveParallaxZone>> RenderParallaxBackground(
      ViewportTab& tab, const Level& level, const ViewportRenderOptions& options) {
    return tab.RenderParallaxBackground(level, options);
  }

  static absl::Status RenderTerrainGhost(ViewportTab& tab, const ViewportRenderOptions& options,
                                         const Tileset* tileset, Vec world_pos) {
    return tab.RenderTerrainGhost(options, tileset, TextureHandle{}, world_pos);
  }
};

namespace {

using ::testing::NiceMock;

// PickEntity tests

TEST(PickEntityTest, HitEntityWithNoSprite) {
  Entity e = {.id = 1, .active = true, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Center of the default 32x32 box
  EXPECT_EQ(PickEntity(entities, {100, 200}, {}).value(), 1u);
}

TEST(PickEntityTest, MissReturnsFallbackId) {
  Entity e = {.id = 1, .active = true, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Well outside the 32x32 hit box
  EXPECT_EQ(PickEntity(entities, {500, 500}, {}).value(), Entity::kInvalidId);
}

TEST(PickEntityTest, InactiveEntityIsSkipped) {
  Entity e = {.id = 1, .active = false, .transform = {.position = {100, 200}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  EXPECT_EQ(PickEntity(entities, {100, 200}, {}).value(), Entity::kInvalidId);
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
  Entity e = {.id = 7, .active = true, .transform = {.position = {50, 100}}, .sprite_id = "s1"};
  std::map<uint64_t, Entity> entities{{e.id, e}};
  const SpriteLookup sprites{{"s1", ResolvedSprite{.sprite = &sprite}}};

  EXPECT_EQ(PickEntity(entities, {55, 95}, sprites).value(), 7u);
  EXPECT_EQ(PickEntity(entities, {35, 95}, sprites).value(), Entity::kInvalidId);
}

// Picking has to agree with drawing. The renderer draws in ascending sort_order,
// so the entity a user sees under the cursor is the one with the highest value,
// whichever way round the IDs happen to run.
TEST(PickEntityTest, OverlappingEntitiesPickTheTopmostDrawn) {
  std::map<uint64_t, Entity> entities{
      {1, Entity{.id = 1, .transform = {.position = {100, 200}}, .sort_order = 4}},
      {2, Entity{.id = 2, .transform = {.position = {100, 200}}, .sort_order = 9}},
      {3, Entity{.id = 3, .transform = {.position = {100, 200}}, .sort_order = -1}},
  };

  EXPECT_EQ(PickEntity(entities, {100, 200}, {}).value(), 2u);
}

// The stable sort leaves equal values in ascending ID order, so the last one
// drawn -- and therefore the one picked -- is the highest ID.
TEST(PickEntityTest, EqualDrawOrderPicksTheHighestId) {
  std::map<uint64_t, Entity> entities{
      {1, Entity{.id = 1, .transform = {.position = {100, 200}}}},
      {5, Entity{.id = 5, .transform = {.position = {100, 200}}}},
      {3, Entity{.id = 3, .transform = {.position = {100, 200}}}},
  };

  EXPECT_EQ(PickEntity(entities, {100, 200}, {}).value(), 5u);
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
  EXPECT_TRUE(e.sprite_id.empty());
  EXPECT_TRUE(e.collider_id.empty());
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

TEST(CreateEntityFromBlueprintTest, InvisibleBlueprintSpriteRemainsNull) {
  // A blueprint with a state but no sprite_id is "invisible".
  Blueprint bp{.id = "invisible-bp", .states = {Blueprint::State{.name = "Idle"}}};
  ASSERT_FALSE(bp.sprite_id(0).has_value()) << "Precondition: blueprint has no sprite";

  Entity e = CreateEntityFromBlueprint(bp, /*state_index=*/0, {100, 200}, /*id=*/5);

  EXPECT_EQ(e.id, 5u);
  EXPECT_EQ(e.blueprint_id, "invisible-bp");
  EXPECT_TRUE(e.sprite_id.empty());
}

TEST(CreateEntityFromBlueprintTest, InvisibleBlueprintNoStatesSpriteRemainsNull) {
  // A blueprint with no states at all also has no sprite.
  Blueprint bp{.id = "empty-bp"};
  ASSERT_FALSE(bp.sprite_id(0).has_value()) << "Precondition: blueprint has no states";

  Entity e = CreateEntityFromBlueprint(bp, /*state_index=*/0, {0, 0}, /*id=*/1);

  EXPECT_TRUE(e.sprite_id.empty());
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
  ViewportTabTestPeer::ApplyPendingCameraFrame(tab, ImVec2(1000, 800),
                                               {.min = {0, 0}, .max = {1000, 1000}});

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

// A tile is stored as a bare ID resolved against the level's own tileset, so
// there is nothing to resolve it against when the level has no tileset.
TEST(ViewportTabTest, RenderRejectsTilePlacementIntoAnUnboundLevel) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);
  Level level{.tile_render_width = 16, .tile_render_height = 16, .width = 100, .height = 100};
  const Tile tile{.id = 3};

  EXPECT_EQ(tab.Render({.level = &level, .placement_tile = &tile}).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportTabTest, RenderRejectsTerrainPaintingWithoutATerrainIndex) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);
  Level level{.tile_render_width = 16, .tile_render_height = 16, .width = 100, .height = 100};

  EXPECT_EQ(tab.Render({.level = &level, .paint_terrain_id = 1}).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ViewportTabTest, SelectedParallaxPreviewRequiresCompatibleSelection) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);

  ViewportTabTestPeer::SetParallaxPreviewMode(tab, ParallaxPreviewMode::kSelectedLayer);
  ViewportTabTestPeer::ReconcileParallaxPreviewMode(
      tab, {.selected_parallax_theme_id = 3, .selected_parallax_layer_index = 1});
  EXPECT_EQ(ViewportTabTestPeer::GetParallaxPreviewMode(tab), ParallaxPreviewMode::kSelectedLayer);

  ViewportTabTestPeer::ReconcileParallaxPreviewMode(tab, {.selected_parallax_theme_id = 3});
  EXPECT_EQ(ViewportTabTestPeer::GetParallaxPreviewMode(tab), ParallaxPreviewMode::kActiveZone);

  ViewportTabTestPeer::SetParallaxPreviewMode(tab, ParallaxPreviewMode::kSelectedTheme);
  ViewportTabTestPeer::ReconcileParallaxPreviewMode(tab, {});
  EXPECT_EQ(ViewportTabTestPeer::GetParallaxPreviewMode(tab), ParallaxPreviewMode::kActiveZone);
}

TEST(ViewportTabTest, ActiveZoneWithoutAssignedThemeDoesNotFailPreview) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);
  Level level;
  level.zones.push_back({
      .id = 4,
      .theme_id = -1,
      .min_point = {-10, -10},
      .max_point = {10, 10},
  });

  auto active = ViewportTabTestPeer::RenderParallaxBackground(tab, level, {});

  ASSERT_OK(active);
  ASSERT_TRUE(active->has_value());
  EXPECT_EQ(active->value().zone_id, 4);
}

// SnapEntityToGrid tests

TEST(SnapEntityToGridTest, ColliderCenterAlignedAndBottomAligned) {
  // Tile grid: 16×16. Mouse at (20, 20) → tile (1, 1): center_x=24, bottom_y=32.
  // Collider: a box from x∈[-8,8], y∈[-16,0] → center_x_offset=0, max_y=0.
  Collider collider;
  collider.polygons.push_back({{-8, -16}, {8, -16}, {8, 0}, {-8, 0}});
  absl::StatusOr<Vec> result = SnapEntityToGrid({20, 20}, 16, 16, &collider, nullptr);
  ASSERT_OK(result);
  EXPECT_DOUBLE_EQ(result->x, 24.0);  // cell_center_x - 0
  EXPECT_DOUBLE_EQ(result->y, 32.0);  // cell_bottom_y - 0
}

TEST(SnapEntityToGridTest, ColliderAsymmetricBoundingBox) {
  // Tile grid: 16×16. Mouse at (0, 0) → tile (0, 0): center_x=8, bottom_y=16.
  // Collider x∈[4,12] → center_x_offset=8; max_y=10.
  Collider collider;
  collider.polygons.push_back({{4, 0}, {12, 0}, {12, 10}, {4, 10}});
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, &collider, nullptr);
  ASSERT_OK(result);
  EXPECT_DOUBLE_EQ(result->x, 0.0);  // 8 - 8
  EXPECT_DOUBLE_EQ(result->y, 6.0);  // 16 - 10
}

TEST(SnapEntityToGridTest, SpriteFallbackWhenNoCollider) {
  // Tile grid: 16×16. Mouse at (0, 0) → tile (0, 0): center_x=8, bottom_y=16.
  // Sprite: render_w=48, render_h=64, offset_x=-24, offset_y=0.
  // center_x_offset = -24 + 24 = 0; bottom_y_offset = 0 + 64 = 64.
  Sprite sprite;
  sprite.frames.push_back(
      SpriteFrame{.render_w = 48, .render_h = 64, .offset_x = -24, .offset_y = 0});
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, nullptr, &sprite);
  ASSERT_OK(result);
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
  ASSERT_OK(result);
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

  absl::StatusOr<Vec> result =
      SnapEntityToGrid({std::numeric_limits<double>::max(), 0}, 16, 16, nullptr, &sprite);

  EXPECT_EQ(result.status().code(), absl::StatusCode::kOutOfRange);
}

TEST(SnapEntityToGridTest, EmptyColliderFallsBackToSprite) {
  // Collider present but has no polygons — falls through to sprite.
  Collider collider;  // polygons empty
  Sprite sprite;
  sprite.frames.push_back(
      SpriteFrame{.render_w = 16, .render_h = 16, .offset_x = 0, .offset_y = 0});
  // Tile grid: 16×16. Mouse at (0, 0) → center_x=8, bottom_y=16.
  // Sprite: center_x_offset=8, bottom_y_offset=16 → pos=(0, 0).
  absl::StatusOr<Vec> result = SnapEntityToGrid({0, 0}, 16, 16, &collider, &sprite);
  ASSERT_OK(result);
  EXPECT_DOUBLE_EQ(result->x, 0.0);
  EXPECT_DOUBLE_EQ(result->y, 0.0);
}

// PickEntity already uses a 32x32 default hit-box for entities with no sprite.
// Verify both edges of that box for an entity created from an invisible blueprint.
TEST(PickEntityTest, InvisibleBlueprintEntityHitsDefaultBox) {
  // Simulates an entity placed from an invisible blueprint: sprite is null.
  Entity e = {.id = 3, .active = true, .transform = {.position = {50, 80}}};
  std::map<uint64_t, Entity> entities{{e.id, e}};

  // Hits are inside the default ±16 box around (50, 80): x∈[34,66], y∈[64,96].
  EXPECT_EQ(PickEntity(entities, {50, 80}, {}).value(), 3u);  // center
  EXPECT_EQ(PickEntity(entities, {34, 64}, {}).value(), 3u);  // top-left corner
  EXPECT_EQ(PickEntity(entities, {66, 96}, {}).value(), 3u);  // bottom-right corner

  // Miss just outside the box.
  EXPECT_EQ(PickEntity(entities, {33, 80}, {}).value(), Entity::kInvalidId);
  EXPECT_EQ(PickEntity(entities, {67, 80}, {}).value(), Entity::kInvalidId);
}

TEST(PickEntityTest, InvalidSpriteBoundsFailFast) {
  Sprite sprite;
  sprite.frames.push_back(SpriteFrame{.render_w = 0, .render_h = 16});
  Entity entity{.id = 1, .sprite_id = "s1"};
  std::map<uint64_t, Entity> entities{{entity.id, entity}};
  const SpriteLookup sprites{{"s1", ResolvedSprite{.sprite = &sprite}}};

  EXPECT_EQ(PickEntity(entities, {0, 0}, sprites).status().code(),
            absl::StatusCode::kInvalidArgument);
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

// Records which question the ghost asked. The two differ in exactly the way
// that matters: one creates artwork, the other does not.
class RecordingTileProvider : public TerrainTileProvider {
 public:
  absl::StatusOr<int> TileForKey(const Terrain&, const TerrainCellKey&, int, int) override {
    ++resolves;
    return 1;
  }

  absl::StatusOr<TerrainPreview> PreviewForKey(const Terrain&, const TerrainCellKey&, int,
                                               int) override {
    ++previews;
    RgbaImage artwork;
    artwork.width = 32;
    artwork.height = 32;
    artwork.pixels.assign(32 * 32 * 4, 0);
    return TerrainPreview{.artwork = std::move(artwork)};
  }

  int resolves = 0;
  int previews = 0;
};

// The ghost must ask what a cell *would* get, never take it. Resolving instead
// appended a tile and grew the atlas on mouse movement alone -- and the grown
// atlas is not uploaded until the frame ends, so the ghost then drew itself
// against a texture that was still the old size and failed the frame.
TEST(TerrainGhostTest, HoveringPreviewsRatherThanResolving) {
  NiceMock<MockApi> api;
  NiceMock<MockGui> gui;
  ViewportTab tab(api, &gui);

  Tileset tileset;
  tileset.tile_width = 32;
  tileset.tile_height = 32;
  Terrain terrain;
  terrain.id = 1;
  terrain.name = "Cave";
  terrain.scheme = TerrainScheme::kDerived;
  tileset.terrains.push_back(terrain);

  absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
  ASSERT_OK(index);

  Level level;
  level.width = 640;
  level.height = 640;
  RecordingTileProvider provider;
  ViewportRenderOptions options{
      .level = &level,
      .paint_terrain_id = 1,
      .terrain_index = &*index,
      .terrain_provider = &provider,
  };

  ASSERT_OK(ViewportTabTestPeer::RenderTerrainGhost(tab, options, &tileset, {64, 64}));

  EXPECT_EQ(provider.previews, 1);
  EXPECT_EQ(provider.resolves, 0) << "hovering must not create artwork";
}

}  // namespace
}  // namespace zebes
