#include "resources/asset_references.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace zebes {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

// Every collection has to be supplied, so the fixture owns them and hands out
// one catalog. A scan over a catalog missing a collection would silently report
// no references from it.
struct Catalogs {
  std::vector<Tileset> tilesets;
  std::vector<Sprite> sprites;
  std::vector<Blueprint> blueprints;
  std::vector<Level> levels;
  std::vector<TerrainRecipe> recipes;

  AssetCatalog View() const { return {tilesets, sprites, blueprints, levels, recipes}; }
};

Level LevelWithParallax(std::string id, std::string name, std::string theme_name,
                        std::string layer_name, std::string texture_id) {
  Level level;
  level.id = std::move(id);
  level.name = std::move(name);
  level.themes[1] = ParallaxTheme{
      .id = 1,
      .name = std::move(theme_name),
      .layers = {ParallaxLayer{.name = std::move(layer_name), .texture_id = std::move(texture_id)}},
  };
  return level;
}

Level LevelWithTiles(std::string id, std::string name, std::string tileset_id,
                     const std::vector<int>& painted) {
  Level level;
  level.id = std::move(id);
  level.name = std::move(name);
  level.tileset_id = std::move(tileset_id);
  TileChunk chunk;
  for (size_t i = 0; i < painted.size(); ++i) chunk.tiles[i] = painted[i];
  level.layers.front().tile_chunks[0] = chunk;
  return level;
}

// --- Textures ----------------------------------------------------------------

TEST(AssetReferencesTest, FindsATextureAcrossEveryKindThatCanNameOne) {
  Catalogs c;
  c.tilesets.push_back(Tileset{.id = "ts", .name = "Cave", .texture_id = "tex"});
  c.sprites.push_back(Sprite{.id = "sp", .name = "Crystal", .texture_id = "tex"});
  c.levels.push_back(LevelWithParallax("lv", "Donut Plains", "Sky", "Clouds", "tex"));
  c.recipes.push_back(TerrainRecipe{.id = "rc", .name = "Cave", .texture_id = "tex"});

  const std::vector<AssetReference> referrers = FindTextureReferrers(c.View(), "tex");

  EXPECT_THAT(referrers, ElementsAre(Field(&AssetReference::kind, AssetKind::kTileset),
                                     Field(&AssetReference::kind, AssetKind::kSprite),
                                     Field(&AssetReference::kind, AssetKind::kLevel),
                                     Field(&AssetReference::kind, AssetKind::kTerrainRecipe)));
}

// A level may hold many themes, so "Level references this" would leave the user
// hunting for which layer to change.
TEST(AssetReferencesTest, AParallaxReferenceNamesItsThemeAndLayer) {
  Catalogs c;
  c.levels.push_back(LevelWithParallax("lv", "Donut Plains", "Sky", "Clouds", "tex"));

  const std::vector<AssetReference> referrers = FindTextureReferrers(c.View(), "tex");

  ASSERT_EQ(referrers.size(), 1u);
  EXPECT_EQ(referrers[0].display_name, "Donut Plains");
  EXPECT_THAT(referrers[0].field, HasSubstr("Sky"));
  EXPECT_THAT(referrers[0].field, HasSubstr("Clouds"));
}

// An unset texture on a layer is a valid unfinished layer, not a pointer at
// whatever is being deleted. Reporting it would block every deletion at once.
TEST(AssetReferencesTest, AnEmptyIdIsNotAReference) {
  Catalogs c;
  c.tilesets.push_back(Tileset{.id = "ts", .name = "Cave", .texture_id = ""});
  c.sprites.push_back(Sprite{.id = "sp", .name = "Crystal", .texture_id = ""});
  c.levels.push_back(LevelWithParallax("lv", "Level", "Sky", "Empty Layer", ""));

  EXPECT_THAT(FindTextureReferrers(c.View(), ""), IsEmpty());
  EXPECT_THAT(FindTextureReferrers(c.View(), "tex"), IsEmpty());
}

TEST(AssetReferencesTest, AnUnreferencedTextureHasNoReferrers) {
  Catalogs c;
  c.tilesets.push_back(Tileset{.id = "ts", .name = "Cave", .texture_id = "other"});

  EXPECT_THAT(FindTextureReferrers(c.View(), "tex"), IsEmpty());
}

// --- Tilesets ----------------------------------------------------------------

TEST(AssetReferencesTest, FindsLevelsAndRecipesNamingATileset) {
  Catalogs c;
  Level level;
  level.id = "lv";
  level.name = "Cave Level";
  level.tileset_id = "ts";
  c.levels.push_back(std::move(level));
  c.recipes.push_back(TerrainRecipe{.id = "rc", .name = "Cave", .tileset_id = "ts"});

  const std::vector<AssetReference> referrers = FindTilesetReferrers(c.View(), "ts");

  ASSERT_EQ(referrers.size(), 2u);
  EXPECT_EQ(referrers[0].kind, AssetKind::kLevel);
  EXPECT_EQ(referrers[1].kind, AssetKind::kTerrainRecipe);
}

// --- Sprites, colliders, blueprints ------------------------------------------

// An entity carries its own asset IDs rather than resolving through its
// blueprint each frame, so a level can name a sprite no blueprint does.
TEST(AssetReferencesTest, FindsASpriteNamedByABlueprintStateAndByAnEntity) {
  Catalogs c;
  Blueprint blueprint;
  blueprint.id = "bp";
  blueprint.name = "Tree";
  blueprint.states.push_back(Blueprint::State{.name = "Idle", .sprite_id = "sp"});
  c.blueprints.push_back(std::move(blueprint));

  Level level;
  level.id = "lv";
  level.name = "Forest";
  Entity entity;
  entity.id = 42;
  entity.sprite_id = "sp";
  level.layers.front().entities.emplace(42, std::move(entity));
  c.levels.push_back(std::move(level));

  const std::vector<AssetReference> referrers = FindSpriteReferrers(c.View(), "sp");

  ASSERT_EQ(referrers.size(), 2u);
  EXPECT_EQ(referrers[0].kind, AssetKind::kBlueprint);
  EXPECT_THAT(referrers[0].field, HasSubstr("Idle"));
  EXPECT_EQ(referrers[1].kind, AssetKind::kLevel);
  EXPECT_THAT(referrers[1].field, HasSubstr("42"));
}

TEST(AssetReferencesTest, FindsAColliderNamedByABlueprintState) {
  Catalogs c;
  Blueprint blueprint;
  blueprint.id = "bp";
  blueprint.name = "Tree";
  blueprint.states.push_back(Blueprint::State{.name = "Idle", .collider_id = "col"});
  c.blueprints.push_back(std::move(blueprint));

  EXPECT_EQ(FindColliderReferrers(c.View(), "col").size(), 1u);
  EXPECT_THAT(FindColliderReferrers(c.View(), "other"), IsEmpty());
}

TEST(AssetReferencesTest, FindsABlueprintPlacedInALevel) {
  Catalogs c;
  Level level;
  level.id = "lv";
  level.name = "Forest";
  Entity entity;
  entity.id = 7;
  entity.blueprint_id = "bp";
  level.layers.front().entities.emplace(7, std::move(entity));
  c.levels.push_back(std::move(level));

  const std::vector<AssetReference> referrers = FindBlueprintReferrers(c.View(), "bp");

  ASSERT_EQ(referrers.size(), 1u);
  EXPECT_EQ(referrers[0].display_name, "Forest");
}

// --- Tiles -------------------------------------------------------------------

TEST(AssetReferencesTest, FindsLevelsThatPaintedATile) {
  Catalogs c;
  c.levels.push_back(LevelWithTiles("lv", "Cave Level", "ts", {5, 5, 9, 0, 5}));

  const std::vector<AssetReference> referrers = FindTileReferrers(c.View(), "ts", 5);

  ASSERT_EQ(referrers.size(), 1u);
  EXPECT_EQ(referrers[0].display_name, "Cave Level");
  // The count, because a world is too big to hunt for the cells by eye.
  EXPECT_THAT(referrers[0].field, HasSubstr("3 painted cells"));
}

// A level stores tile IDs as bare integers, so the same number means different
// artwork under a different tileset. Without the tileset filter this would
// report every level that happened to paint the same number.
TEST(AssetReferencesTest, ATileIsOnlyReferencedByLevelsBoundToItsTileset) {
  Catalogs c;
  c.levels.push_back(LevelWithTiles("a", "Bound", "ts", {5}));
  c.levels.push_back(LevelWithTiles("b", "Other Tileset", "other", {5}));

  const std::vector<AssetReference> referrers = FindTileReferrers(c.View(), "ts", 5);

  ASSERT_EQ(referrers.size(), 1u);
  EXPECT_EQ(referrers[0].display_name, "Bound");
}

// Zero is the empty cell, not a tile, so a chunk full of them references nothing.
TEST(AssetReferencesTest, TheEmptyCellIsNotATileReference) {
  Catalogs c;
  c.levels.push_back(LevelWithTiles("lv", "Cave Level", "ts", {0, 0, 0}));

  EXPECT_THAT(FindTileReferrers(c.View(), "ts", 0), IsEmpty());
}

TEST(AssetReferencesTest, SingularAndPluralPaintedCells) {
  Catalogs c;
  c.levels.push_back(LevelWithTiles("lv", "Cave Level", "ts", {5}));

  EXPECT_THAT(FindTileReferrers(c.View(), "ts", 5)[0].field, HasSubstr("1 painted cell"));
}

// --- The message -------------------------------------------------------------

TEST(AssetReferencesTest, ARefusalNamesEveryReferrer) {
  const std::vector<AssetReference> referrers{
      AssetReference{.kind = AssetKind::kTileset,
                     .id = "ts",
                     .display_name = "lucinda_cave",
                     .field = "texture_id"},
      AssetReference{.kind = AssetKind::kLevel,
                     .id = "lv",
                     .display_name = "Donut Plains",
                     .field = "theme 'Sky', layer 'Clouds'"},
  };

  const std::string message = DescribeBlockedDeletion("texture 'lucinda_cave'", referrers);

  EXPECT_THAT(message, HasSubstr("2 things reference it"));
  EXPECT_THAT(message, HasSubstr("Tileset 'lucinda_cave' (texture_id)"));
  EXPECT_THAT(message, HasSubstr("Level 'Donut Plains' (theme 'Sky', layer 'Clouds')"));
}

TEST(AssetReferencesTest, ARefusalForOneReferrerReadsAsSingular) {
  const std::vector<AssetReference> referrers{
      AssetReference{.kind = AssetKind::kSprite, .display_name = "Crystal", .field = "texture_id"}};

  EXPECT_THAT(DescribeBlockedDeletion("texture 'cave'", referrers),
              HasSubstr("1 thing references it"));
}

}  // namespace
}  // namespace zebes
