// Guards the definitions that actually ship in assets/.
//
// LoadTileset deliberately skips ValidateTileset -- validation runs on save --
// so a definition that was hand-edited, half-migrated, or left pointing at
// artwork that has since been replaced loads silently and only fails when
// something tries to paint with it. These tests close that gap by asserting the
// shipped definitions satisfy the same invariants a save would enforce.
//
// They are also what makes strict parsing affordable. Every reader in
// resources/ requires each field its writer emits, so a definition left behind
// by a format change fails to load rather than being silently reinterpreted.
// That is only a safe trade if something notices, and loading every shipped
// definition of every kind here is that something -- otherwise the first report
// would come from whoever opened the editor next.

#include <filesystem>
#include <fstream>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "editor/level_editor/terrain_brush.h"
#include "gtest/gtest.h"
#include "macros.h"
#include "nlohmann/json.hpp"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/fake_texture_resource_store.h"
#include "resources/level_manager.h"
#include "resources/parallax_theme_manager.h"
#include "resources/prop_recipe_manager.h"
#include "resources/source_artwork_manager.h"
#include "resources/sprite_manager.h"
#include "resources/texture_manager.h"
#include "resources/tileset_manager.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

// Absolute path to the repository's assets directory, injected by CMake so the
// test does not depend on the working directory ctest happens to use.
constexpr char kAssetsRoot[] = ZEBES_TEST_ASSETS_DIR;

std::string TilesetDefinitionsDir() { return std::string(kAssetsRoot) + "/definitions/tilesets"; }

// Maps every texture definition's ID to the image path it declares. Read
// directly rather than through TextureManager, which needs a live SDL resource
// store; only the declared path matters here.
absl::flat_hash_map<std::string, std::string> TexturePathsById() {
  absl::flat_hash_map<std::string, std::string> paths;
  const std::string dir = std::string(kAssetsRoot) + "/definitions/textures";
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".json") continue;
    std::ifstream stream(entry.path());
    nlohmann::json json;
    stream >> json;
    paths[json.at("id").get<std::string>()] = json.at("path").get<std::string>();
  }
  return paths;
}

std::vector<Tileset> LoadShippedTilesets() {
  absl::StatusOr<std::unique_ptr<TilesetManager>> manager = TilesetManager::Create(kAssetsRoot);
  EXPECT_OK(manager);
  if (!manager.ok()) return {};

  const absl::Status loaded = (*manager)->LoadAllTilesets();
  EXPECT_OK(loaded);
  if (!loaded.ok()) return {};

  return (*manager)->GetAllTilesets();
}

// The directory must not be empty, or every test below would pass vacuously.
TEST(ShippedAssetsTest, ThereAreTilesetsToCheck) {
  EXPECT_FALSE(LoadShippedTilesets().empty())
      << "no tilesets loaded from " << TilesetDefinitionsDir();
}

TEST(ShippedAssetsTest, EveryShippedTilesetValidates) {
  for (const Tileset& tileset : LoadShippedTilesets()) {
    const absl::Status status = ValidateTileset(tileset);
    EXPECT_OK(status) << "tileset '" << tileset.name << "'";
  }
}

// Catches the failure mode where a texture's underlying artwork is replaced or
// removed while definitions keep pointing at it.
TEST(ShippedAssetsTest, EveryTilesetTextureResolvesToAFileOnDisk) {
  const absl::flat_hash_map<std::string, std::string> texture_paths = TexturePathsById();

  for (const Tileset& tileset : LoadShippedTilesets()) {
    const auto found = texture_paths.find(tileset.texture_id);
    ASSERT_NE(found, texture_paths.end())
        << "tileset '" << tileset.name << "' references unknown texture ID " << tileset.texture_id;

    const std::string image = ResolveTextureImagePath(kAssetsRoot, found->second);
    EXPECT_TRUE(std::filesystem::exists(image))
        << "tileset '" << tileset.name << "' texture file is missing: " << image;
  }
}

// Every texture definition should point at real artwork, whether or not a
// tileset happens to use it.
TEST(ShippedAssetsTest, EveryTextureDefinitionPointsAtRealArtwork) {
  const absl::flat_hash_map<std::string, std::string> texture_paths = TexturePathsById();
  ASSERT_FALSE(texture_paths.empty());

  for (const auto& [id, relative] : texture_paths) {
    const std::string image = ResolveTextureImagePath(kAssetsRoot, relative);
    EXPECT_TRUE(std::filesystem::exists(image))
        << "texture " << id << " points at missing file: " << image;
  }
}

// Loading is the assertion. Every reader requires each field its writer emits,
// so a definition left behind by a format change fails here rather than in
// front of whoever opens the editor next.
//
// The count is checked against the files on disk rather than merely being
// non-zero, because a bulk load reports failures but still returns whatever it
// managed to read: "loaded something" would pass while half the catalog was
// missing.
size_t DefinitionFileCount(const std::string& kind) {
  size_t count = 0;
  const std::string dir = std::string(kAssetsRoot) + "/definitions/" + kind;
  if (!std::filesystem::exists(dir)) return 0;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".json") ++count;
  }
  return count;
}

TEST(ShippedAssetsTest, EveryShippedSourceArtworkLoads) {
  const std::string directory = std::string(kAssetsRoot) + "/definitions/source_artworks";
  if (!std::filesystem::exists(directory)) {
    EXPECT_EQ(DefinitionFileCount("source_artworks"), 0);
    return;
  }

  absl::StatusOr<std::unique_ptr<SourceArtworkManager>> manager =
      SourceArtworkManager::Create(kAssetsRoot);
  ASSERT_OK(manager);

  const absl::Status loaded = (*manager)->LoadAllArtwork();
  EXPECT_OK(loaded);
  EXPECT_EQ((*manager)->GetAllArtwork().size(), DefinitionFileCount("source_artworks"));
}

TEST(ShippedAssetsTest, EveryShippedPropRecipeLoads) {
  const std::string directory = std::string(kAssetsRoot) + "/definitions/prop_recipes";
  if (!std::filesystem::exists(directory)) {
    EXPECT_EQ(DefinitionFileCount("prop_recipes"), 0);
    return;
  }

  absl::StatusOr<std::unique_ptr<PropRecipeManager>> manager =
      PropRecipeManager::Create(kAssetsRoot);
  ASSERT_OK(manager);

  const absl::Status loaded = (*manager)->LoadAllRecipes();
  EXPECT_OK(loaded);
  EXPECT_EQ((*manager)->GetAllRecipes().size(), DefinitionFileCount("prop_recipes"));
}

TEST(ShippedAssetsTest, EveryShippedLevelLoads) {
  absl::StatusOr<std::unique_ptr<LevelManager>> manager = LevelManager::Create(kAssetsRoot);
  ASSERT_OK(manager);

  const absl::Status loaded = (*manager)->LoadAllLevels();
  EXPECT_OK(loaded);
  EXPECT_EQ((*manager)->GetAllLevels().size(), DefinitionFileCount("levels"));
}

TEST(ShippedAssetsTest, EveryShippedParallaxThemeLoadsAndReferencesArtwork) {
  absl::StatusOr<std::unique_ptr<ParallaxThemeManager>> manager =
      ParallaxThemeManager::Create(kAssetsRoot);
  ASSERT_OK(manager);
  ASSERT_OK((*manager)->LoadAllThemes());
  const std::vector<ParallaxTheme> themes = (*manager)->GetAllThemes();
  EXPECT_EQ(themes.size(), DefinitionFileCount("parallax_themes"));

  const absl::flat_hash_map<std::string, std::string> textures = TexturePathsById();
  for (const ParallaxTheme& theme : themes) {
    EXPECT_OK(ValidateParallaxTheme(theme)) << theme.name;
    for (const ParallaxLayer& layer : theme.layers) {
      EXPECT_TRUE(textures.contains(layer.texture_id))
          << "theme '" << theme.name << "' layer '" << layer.name << "' references missing texture "
          << layer.texture_id;
    }
  }
}

TEST(ShippedAssetsTest, EveryLevelZoneReferencesAShippedParallaxTheme) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<LevelManager> levels, LevelManager::Create(kAssetsRoot));
  ASSERT_OK(levels->LoadAllLevels());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ParallaxThemeManager> themes,
                       ParallaxThemeManager::Create(kAssetsRoot));
  ASSERT_OK(themes->LoadAllThemes());

  for (const Level& level : levels->GetAllLevels()) {
    for (const ParallaxZone& zone : level.zones) {
      EXPECT_OK(themes->GetTheme(zone.theme_id).status())
          << "level '" << level.name << "' zone '" << zone.name << "' references missing theme "
          << zone.theme_id;
    }
  }
}

TEST(ShippedAssetsTest, EveryShippedSpriteLoads) {
  // A sprite names its texture by ID and refuses to load without it, so the
  // texture catalog has to exist first. The store is faked because only the
  // definitions matter here; no image is decoded.
  FakeTextureResourceStore store;
  absl::StatusOr<std::unique_ptr<TextureManager>> textures =
      TextureManager::Create(&store, kAssetsRoot);
  ASSERT_OK(textures);
  ASSERT_OK((*textures)->LoadAllTextures());

  absl::StatusOr<std::unique_ptr<SpriteManager>> manager =
      SpriteManager::Create(textures->get(), kAssetsRoot);
  ASSERT_OK(manager);

  const absl::Status loaded = (*manager)->LoadAllSprites();
  EXPECT_OK(loaded);
  EXPECT_EQ((*manager)->GetAllSprites().size(), DefinitionFileCount("sprites"));
}

TEST(ShippedAssetsTest, EveryShippedBlueprintLoads) {
  absl::StatusOr<std::unique_ptr<BlueprintManager>> manager = BlueprintManager::Create(kAssetsRoot);
  ASSERT_OK(manager);

  const absl::Status loaded = (*manager)->LoadAllBlueprints();
  EXPECT_OK(loaded);
  EXPECT_EQ((*manager)->GetAllBlueprints().size(), DefinitionFileCount("blueprints"));
}

TEST(ShippedAssetsTest, EveryShippedColliderLoads) {
  absl::StatusOr<std::unique_ptr<ColliderManager>> manager = ColliderManager::Create(kAssetsRoot);
  ASSERT_OK(manager);

  const absl::Status loaded = (*manager)->LoadAllColliders();
  EXPECT_OK(loaded);
  EXPECT_EQ((*manager)->GetAllColliders().size(), DefinitionFileCount("colliders"));
}

// Every image in assets/textures/ must have a definition naming it.
//
// The rule exists because a tileset once referenced a file that had been
// replaced with unrelated artwork, with nothing to catch it: an image nothing
// declares is unreachable, and indistinguishable by eye from art still in use.
// Inputs the tools crop from live in assets/source_art/ and correctly have no
// definition.
TEST(ShippedAssetsTest, EveryShippedImageHasADefinitionNamingIt) {
  absl::flat_hash_set<std::string> declared;
  for (const auto& [id, path] : TexturePathsById()) {
    declared.insert(std::filesystem::path(path).filename().string());
  }

  const std::string images = std::string(kAssetsRoot) + "/textures";
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(images)) {
    if (!entry.is_regular_file()) continue;
    const std::string filename = entry.path().filename().string();
    EXPECT_TRUE(declared.contains(filename))
        << filename << " is in assets/textures/ but no texture definition names it";
  }
}

// A kBlob47 terrain missing even one mask makes the brush fail mid-stroke, so
// its table has to be complete rather than merely non-empty.
//
// The check is scoped to that scheme because TerrainScheme is a tagged union: a
// kDerived terrain has no rule table at all, since a mask cannot say that a
// neighbour is a wedge and the artwork is rendered for the neighbourhood the
// level actually has. Asserting mask coverage there would demand the very
// enumeration that scheme exists to avoid.
TEST(ShippedAssetsTest, EveryBlob47TerrainIsPaintableForAllFortySevenMasks) {
  for (const Tileset& tileset : LoadShippedTilesets()) {
    absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
    ASSERT_OK(index) << "tileset '" << tileset.name << "'";

    for (const Terrain& terrain : tileset.terrains) {
      if (terrain.scheme != TerrainScheme::kBlob47) continue;

      absl::flat_hash_set<int> covered;
      for (const TerrainRule& rule : terrain.rules) covered.insert(rule.mask);

      for (const uint8_t mask : Blob47MaskTable()) {
        EXPECT_TRUE(covered.contains(mask))
            << "tileset '" << tileset.name << "' terrain '" << terrain.name
            << "' has no rule for mask " << static_cast<int>(mask);
      }
    }
  }
}

// The equivalent completeness check for the other variant. A derived terrain's
// artwork is resolved on demand, so there is no table to be missing an entry
// from -- but every tile it has already had rendered must still be in the
// tileset, or regeneration fails partway with artwork it cannot redraw.
TEST(ShippedAssetsTest, EveryDerivedTerrainRecordsTilesTheTilesetStillHas) {
  for (const Tileset& tileset : LoadShippedTilesets()) {
    absl::flat_hash_set<int> tile_ids;
    for (const Tile& tile : tileset.tiles) tile_ids.insert(tile.id);

    for (const Terrain& terrain : tileset.terrains) {
      if (terrain.scheme != TerrainScheme::kDerived) continue;

      EXPECT_TRUE(terrain.rules.empty())
          << "tileset '" << tileset.name << "' terrain '" << terrain.name
          << "' is derived but carries a mask-keyed rule table";

      for (const DerivedTile& derived : terrain.derived_tiles) {
        EXPECT_TRUE(tile_ids.contains(derived.tile_id))
            << "tileset '" << tileset.name << "' terrain '" << terrain.name
            << "' records artwork for tile " << derived.tile_id
            << ", which the tileset no longer has";
      }
    }
  }
}

}  // namespace
}  // namespace zebes
