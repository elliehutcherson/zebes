// Guards the definitions that actually ship in assets/.
//
// LoadTileset deliberately skips ValidateTileset -- validation runs on save --
// so a definition that was hand-edited, half-migrated, or left pointing at
// artwork that has since been replaced loads silently and only fails when
// something tries to paint with it. These tests close that gap by asserting the
// shipped definitions satisfy the same invariants a save would enforce.

#include <filesystem>
#include <fstream>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "editor/level_editor/terrain_brush.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "resources/texture_manager.h"
#include "resources/tileset_manager.h"
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
