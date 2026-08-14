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
#include "nlohmann/json.hpp"
#include "resources/blueprint_manager.h"
#include "resources/collider_manager.h"
#include "resources/level_manager.h"
#include "resources/sprite_manager.h"
#include "resources/texture_manager.h"
#include "resources/tileset_manager.h"
#include "resources/fake_texture_resource_store.h"
#include "terrain/terrain_mask.h"

namespace zebes {
namespace {

// Absolute path to the repository's assets directory, injected by CMake so the
// test does not depend on the working directory ctest happens to use.
constexpr char kAssetsRoot[] = ZEBES_TEST_ASSETS_DIR;

std::string TilesetDefinitionsDir() {
  return std::string(kAssetsRoot) + "/definitions/tilesets";
}

// Maps every texture definition's ID to the image path it declares. Read
// directly rather than through TextureManager, which needs a live SDL resource
// store; only the declared path matters here.
absl::flat_hash_map<std::string, std::string> TexturePathsById() {
  absl::flat_hash_map<std::string, std::string> paths;
  const std::string dir = std::string(kAssetsRoot) + "/definitions/textures";
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".json") continue;
    std::ifstream stream(entry.path());
    nlohmann::json json;
    stream >> json;
    paths[json.at("id").get<std::string>()] = json.at("path").get<std::string>();
  }
  return paths;
}

std::vector<Tileset> LoadShippedTilesets() {
  absl::StatusOr<std::unique_ptr<TilesetManager>> manager =
      TilesetManager::Create(kAssetsRoot);
  EXPECT_TRUE(manager.ok()) << manager.status();
  if (!manager.ok()) return {};

  const absl::Status loaded = (*manager)->LoadAllTilesets();
  EXPECT_TRUE(loaded.ok()) << loaded;
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
    EXPECT_TRUE(status.ok()) << "tileset '" << tileset.name << "': " << status;
  }
}

// Catches the failure mode where a texture's underlying artwork is replaced or
// removed while definitions keep pointing at it.
TEST(ShippedAssetsTest, EveryTilesetTextureResolvesToAFileOnDisk) {
  const absl::flat_hash_map<std::string, std::string> texture_paths = TexturePathsById();

  for (const Tileset& tileset : LoadShippedTilesets()) {
    const auto found = texture_paths.find(tileset.texture_id);
    ASSERT_NE(found, texture_paths.end())
        << "tileset '" << tileset.name << "' references unknown texture ID "
        << tileset.texture_id;

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
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() == ".json") ++count;
  }
  return count;
}

TEST(ShippedAssetsTest, EveryShippedLevelLoads) {
  absl::StatusOr<std::unique_ptr<LevelManager>> manager = LevelManager::Create(kAssetsRoot);
  ASSERT_TRUE(manager.ok()) << manager.status();

  const absl::Status loaded = (*manager)->LoadAllLevels();
  EXPECT_TRUE(loaded.ok()) << loaded;
  EXPECT_EQ((*manager)->GetAllLevels().size(), DefinitionFileCount("levels"));
}

TEST(ShippedAssetsTest, EveryShippedSpriteLoads) {
  // A sprite names its texture by ID and refuses to load without it, so the
  // texture catalog has to exist first. The store is faked because only the
  // definitions matter here; no image is decoded.
  FakeTextureResourceStore store;
  absl::StatusOr<std::unique_ptr<TextureManager>> textures =
      TextureManager::Create(&store, kAssetsRoot);
  ASSERT_TRUE(textures.ok()) << textures.status();
  ASSERT_TRUE((*textures)->LoadAllTextures().ok());

  absl::StatusOr<std::unique_ptr<SpriteManager>> manager =
      SpriteManager::Create(textures->get(), kAssetsRoot);
  ASSERT_TRUE(manager.ok()) << manager.status();

  const absl::Status loaded = (*manager)->LoadAllSprites();
  EXPECT_TRUE(loaded.ok()) << loaded;
  EXPECT_EQ((*manager)->GetAllSprites().size(), DefinitionFileCount("sprites"));
}

TEST(ShippedAssetsTest, EveryShippedBlueprintLoads) {
  absl::StatusOr<std::unique_ptr<BlueprintManager>> manager =
      BlueprintManager::Create(kAssetsRoot);
  ASSERT_TRUE(manager.ok()) << manager.status();

  const absl::Status loaded = (*manager)->LoadAllBlueprints();
  EXPECT_TRUE(loaded.ok()) << loaded;
  EXPECT_EQ((*manager)->GetAllBlueprints().size(), DefinitionFileCount("blueprints"));
}

TEST(ShippedAssetsTest, EveryShippedColliderLoads) {
  absl::StatusOr<std::unique_ptr<ColliderManager>> manager = ColliderManager::Create(kAssetsRoot);
  ASSERT_TRUE(manager.ok()) << manager.status();

  const absl::Status loaded = (*manager)->LoadAllColliders();
  EXPECT_TRUE(loaded.ok()) << loaded;
  EXPECT_EQ((*manager)->GetAllColliders().size(), DefinitionFileCount("colliders"));
}

// A terrain missing even one mask makes the brush fail mid-stroke, so the table
// has to be complete rather than merely non-empty.
TEST(ShippedAssetsTest, EveryTerrainIsPaintableForAllFortySevenMasks) {
  for (const Tileset& tileset : LoadShippedTilesets()) {
    absl::StatusOr<TerrainIndex> index = TerrainIndex::Build(tileset);
    ASSERT_TRUE(index.ok()) << "tileset '" << tileset.name << "': " << index.status();

    for (const Terrain& terrain : tileset.terrains) {
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

}  // namespace
}  // namespace zebes
