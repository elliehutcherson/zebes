#include "resources/tileset_manager.h"

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "common/utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

using ::testing::HasSubstr;

class TilesetManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "test_tileset_assets_" + GenerateGuid();
    std::filesystem::remove_all(test_dir_);
    std::filesystem::create_directories(test_dir_ + "/definitions/tilesets");

    ASSERT_OK_AND_ASSIGN(manager_, TilesetManager::Create(test_dir_));
  }

  void TearDown() override {
    manager_.reset();
    std::filesystem::remove_all(test_dir_);
  }

  // Forces a full reload from disk by recreating the manager and calling
  // LoadAllTilesets, matching the pattern used in level_manager_test.cc.
  void ReloadFromDisk() {
    manager_.reset();
    ASSERT_OK_AND_ASSIGN(manager_, TilesetManager::Create(test_dir_));
    ASSERT_OK(manager_->LoadAllTilesets());
  }

  std::string test_dir_;
  std::unique_ptr<TilesetManager> manager_;
};

// --- Basic CRUD ---

TEST_F(TilesetManagerTest, CreateAndGetTileset) {
  Tileset tileset{
      .name = "CaveTiles",
      .texture_id = "cave-atlas-uuid",
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));
  EXPECT_FALSE(id.empty());

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  EXPECT_EQ(loaded->id, id);
  EXPECT_EQ(loaded->name, "CaveTiles");
  EXPECT_EQ(loaded->texture_id, "cave-atlas-uuid");
}

TEST_F(TilesetManagerTest, DeleteTileset) {
  Tileset tileset{
      .name = "ToDelete",
      .texture_id = "some-tex",
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));
  ASSERT_OK(manager_->DeleteTileset(id));
  EXPECT_FALSE(manager_->GetTileset(id).ok());

  std::string expected_file =
      test_dir_ + "/definitions/tilesets/ToDelete-" + id + ".json";
  EXPECT_FALSE(std::filesystem::exists(expected_file));
}

TEST_F(TilesetManagerTest, DeleteTileset_NotFound_Fails) {
  absl::Status status = manager_->DeleteTileset("nonexistent-id");
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::IsNotFound(status));
}

TEST_F(TilesetManagerTest, GetTileset_NotFound_Fails) {
  absl::StatusOr<Tileset*> result = manager_->GetTileset("nonexistent-id");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::IsNotFound(result.status()));
}

// --- ID Generation ---

TEST_F(TilesetManagerTest, CreateTileset_AlwaysGeneratesId) {
  Tileset tileset{
      .id = "caller-provided-id",
      .name = "MyTileset",
      .texture_id = "tex",
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));
  EXPECT_NE(id, "caller-provided-id");
  EXPECT_FALSE(id.empty());
}

// --- Serialization Round-Trip ---

TEST_F(TilesetManagerTest, SerializationRoundTrip) {
  Tileset tileset{
      .name = "ForestTiles",
      .texture_id = "forest-atlas-uuid",
      .tile_width = 32,
      .tile_height = 32,
      .tiles =
          {
              Tile{
                  .id = 1,
                  .name = "Grass",
                  .source_x = 0,
                  .source_y = 0,
                  .shape = TileShape::kFullBlock,
                  .tags = {"solid"},
              },
              Tile{
                  .id = 2,
                  .name = "Platform",
                  .source_x = 32,
                  .source_y = 0,
                  .shape = TileShape::kHalfBlockBottom,
                  .is_one_way = true,
                  .tags = {"ice"},
              },
          },
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));

  ReloadFromDisk();

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  EXPECT_EQ(loaded->name, "ForestTiles");
  EXPECT_EQ(loaded->texture_id, "forest-atlas-uuid");
  EXPECT_EQ(loaded->tile_width, 32);
  EXPECT_EQ(loaded->tile_height, 32);

  ASSERT_EQ(loaded->tiles.size(), 2);

  const Tile& t1 = loaded->tiles[0];
  EXPECT_EQ(t1.id, 1);
  EXPECT_EQ(t1.name, "Grass");
  EXPECT_EQ(t1.source_x, 0);
  EXPECT_EQ(t1.source_y, 0);
  EXPECT_EQ(t1.shape, TileShape::kFullBlock);
  EXPECT_FALSE(t1.is_one_way);
  ASSERT_EQ(t1.tags.size(), 1);
  EXPECT_EQ(t1.tags[0], "solid");

  const Tile& t2 = loaded->tiles[1];
  EXPECT_EQ(t2.id, 2);
  EXPECT_EQ(t2.name, "Platform");
  EXPECT_EQ(t2.source_x, 32);
  EXPECT_EQ(t2.source_y, 0);
  EXPECT_EQ(t2.shape, TileShape::kHalfBlockBottom);
  EXPECT_TRUE(t2.is_one_way);
  ASSERT_EQ(t2.tags.size(), 1);
  EXPECT_EQ(t2.tags[0], "ice");
}

TEST_F(TilesetManagerTest, EmptyTileListPersists) {
  Tileset tileset{
      .name = "EmptyTiles",
      .texture_id = "tex-uuid",
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));

  ReloadFromDisk();

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  EXPECT_TRUE(loaded->tiles.empty());
}

// --- Rename ---

TEST_F(TilesetManagerTest, RenameTileset) {
  Tileset tileset{
      .name = "OldName",
      .texture_id = "tex",
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));

  std::string old_file = test_dir_ + "/definitions/tilesets/OldName-" + id + ".json";
  ASSERT_TRUE(std::filesystem::exists(old_file));

  // Work from a copy so the cache retains the old name for rename detection.
  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  Tileset renamed = *loaded;
  renamed.name = "NewName";
  ASSERT_OK(manager_->SaveTileset(renamed));

  std::string new_file = test_dir_ + "/definitions/tilesets/NewName-" + id + ".json";
  EXPECT_TRUE(std::filesystem::exists(new_file));
  EXPECT_FALSE(std::filesystem::exists(old_file));
}

// --- GetAllTilesets ---

TEST_F(TilesetManagerTest, GetAllTilesets) {
  ASSERT_OK(manager_->CreateTileset(Tileset{.name = "A", .texture_id = "tex-a"}));
  ASSERT_OK(manager_->CreateTileset(Tileset{.name = "B", .texture_id = "tex-b"}));

  std::vector<Tileset> all = manager_->GetAllTilesets();
  EXPECT_EQ(all.size(), 2);
}

// --- IsTextureUsed ---

TEST_F(TilesetManagerTest, IsTextureUsed_True) {
  ASSERT_OK(manager_->CreateTileset(Tileset{.name = "T", .texture_id = "shared-tex"}));
  EXPECT_TRUE(manager_->IsTextureUsed("shared-tex"));
}

TEST_F(TilesetManagerTest, IsTextureUsed_False) {
  ASSERT_OK(manager_->CreateTileset(Tileset{.name = "T", .texture_id = "other-tex"}));
  EXPECT_FALSE(manager_->IsTextureUsed("nonexistent-tex"));
}

// --- CreateTileset Validation ---

TEST_F(TilesetManagerTest, CreateTileset_EmptyName_Fails) {
  Tileset tileset{
      .name = "",
      .texture_id = "tex",
  };

  absl::StatusOr<std::string> id = manager_->CreateTileset(std::move(tileset));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("non-empty name"));
}

TEST_F(TilesetManagerTest, CreateTileset_EmptyTextureId_Fails) {
  Tileset tileset{
      .name = "Valid",
      .texture_id = "",
  };

  absl::StatusOr<std::string> id = manager_->CreateTileset(std::move(tileset));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("texture_id cannot be empty"));
}

TEST_F(TilesetManagerTest, CreateTileset_ZeroTileWidth_Fails) {
  Tileset tileset{
      .name = "Valid",
      .texture_id = "tex",
      .tile_width = 0,
  };

  absl::StatusOr<std::string> id = manager_->CreateTileset(std::move(tileset));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("tile_width must be positive"));
}

TEST_F(TilesetManagerTest, CreateTileset_ZeroTileHeight_Fails) {
  Tileset tileset{
      .name = "Valid",
      .texture_id = "tex",
      .tile_height = 0,
  };

  absl::StatusOr<std::string> id = manager_->CreateTileset(std::move(tileset));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("tile_height must be positive"));
}

// --- SaveTileset / Tile Validation ---

TEST_F(TilesetManagerTest, SaveTileset_EmptyId_Fails) {
  Tileset tileset{
      .id = "",
      .name = "Valid",
      .texture_id = "tex",
  };

  absl::Status status = manager_->SaveTileset(tileset);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("must have an ID"));
}

TEST_F(TilesetManagerTest, SaveTileset_ZeroTileId_Fails) {
  Tileset tileset{
      .id = "some-id",
      .name = "Valid",
      .texture_id = "tex",
      .tiles = {Tile{.id = 0, .name = "Empty"}},
  };

  absl::Status status = manager_->SaveTileset(tileset);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("reserved for empty cells"));
}

TEST_F(TilesetManagerTest, SaveTileset_NegativeTileId_Fails) {
  Tileset tileset{
      .id = "some-id",
      .name = "Valid",
      .texture_id = "tex",
      .tiles = {Tile{.id = -1, .name = "Negative"}},
  };

  absl::Status status = manager_->SaveTileset(tileset);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("reserved for empty cells"));
}

TEST_F(TilesetManagerTest, SaveTileset_EmptyTileName_Fails) {
  Tileset tileset{
      .id = "some-id",
      .name = "Valid",
      .texture_id = "tex",
      .tiles = {Tile{.id = 1, .name = ""}},
  };

  absl::Status status = manager_->SaveTileset(tileset);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("must have a name"));
}

TEST_F(TilesetManagerTest, SaveTileset_DuplicateTileId_Fails) {
  Tileset tileset{
      .id = "some-id",
      .name = "Valid",
      .texture_id = "tex",
      .tiles =
          {
              Tile{.id = 1, .name = "Rock"},
              Tile{.id = 1, .name = "AlsoRock"},
          },
  };

  absl::Status status = manager_->SaveTileset(tileset);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("Duplicate tile ID"));
}

// --- LoadTileset Error Cases ---

TEST_F(TilesetManagerTest, LoadTileset_NotFound_Fails) {
  absl::StatusOr<Tileset*> result = manager_->LoadTileset("nonexistent.json");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(absl::IsNotFound(result.status()));
}

TEST_F(TilesetManagerTest, LoadTileset_MissingRequiredField_Fails) {
  std::string file_path =
      test_dir_ + "/definitions/tilesets/bad_tileset.json";
  std::ofstream out(file_path);
  // Missing "texture_id" required field.
  out << R"({"id": "abc", "name": "BadTileset"})";
  out.close();

  EXPECT_FALSE(manager_->LoadTileset("bad_tileset.json").ok());
}

// --- LoadAllTilesets ---

TEST_F(TilesetManagerTest, LoadAllTilesets_PopulatesCache) {
  ASSERT_OK(manager_->CreateTileset(Tileset{.name = "X", .texture_id = "tx"}));
  ASSERT_OK(manager_->CreateTileset(Tileset{.name = "Y", .texture_id = "ty"}));

  ReloadFromDisk();

  EXPECT_EQ(manager_->GetAllTilesets().size(), 2);
}

// --- Terrains ---

namespace {

// A tileset with three tiles and one terrain whose solid mask offers two
// variants, exercising the variety path through serialization.
Tileset MakeTerrainTileset() {
  return Tileset{
      .name = "GrassTerrain",
      .texture_id = "grass-atlas-uuid",
      .tile_width = 32,
      .tile_height = 32,
      .tiles =
          {
              Tile{.id = 1, .name = "Isolated"},
              Tile{.id = 2, .name = "SolidA", .source_x = 32},
              Tile{.id = 3, .name = "SolidB", .source_x = 64},
          },
      .terrains =
          {
              Terrain{
                  .id = 7,
                  .name = "Grass",
                  .scheme = TerrainScheme::kBlob47,
                  .solid_outside_level = false,
                  .rules =
                      {
                          TerrainRule{.mask = 0, .variants = {{.tile_id = 1, .weight = 1}}},
                          TerrainRule{.mask = 255,
                                      .variants = {{.tile_id = 2, .weight = 3},
                                                   {.tile_id = 3, .weight = 1}}},
                      },
              },
          },
  };
}

}  // namespace

TEST_F(TilesetManagerTest, TerrainSurvivesSerializationRoundTrip) {
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(MakeTerrainTileset()));

  ReloadFromDisk();

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  ASSERT_EQ(loaded->terrains.size(), 1);

  const Terrain& terrain = loaded->terrains[0];
  EXPECT_EQ(terrain.id, 7);
  EXPECT_EQ(terrain.name, "Grass");
  EXPECT_EQ(terrain.scheme, TerrainScheme::kBlob47);
  EXPECT_FALSE(terrain.solid_outside_level);

  ASSERT_EQ(terrain.rules.size(), 2);
  EXPECT_EQ(terrain.rules[0].mask, 0);
  ASSERT_EQ(terrain.rules[0].variants.size(), 1);
  EXPECT_EQ(terrain.rules[0].variants[0].tile_id, 1);

  EXPECT_EQ(terrain.rules[1].mask, 255);
  ASSERT_EQ(terrain.rules[1].variants.size(), 2);
  EXPECT_EQ(terrain.rules[1].variants[0].tile_id, 2);
  EXPECT_EQ(terrain.rules[1].variants[0].weight, 3);
  EXPECT_EQ(terrain.rules[1].variants[1].tile_id, 3);
  EXPECT_EQ(terrain.rules[1].variants[1].weight, 1);
}

// Every tileset authored before terrains existed must keep loading unchanged.
TEST_F(TilesetManagerTest, LegacyTilesetWithoutTerrainsLoads) {
  std::string file_path = test_dir_ + "/definitions/tilesets/legacy.json";
  std::ofstream out(file_path);
  out << R"({
    "id": "legacy-id",
    "name": "Legacy",
    "texture_id": "tx",
    "tile_width": 32,
    "tile_height": 32,
    "tiles": [{"id": 1, "name": "Solid", "source_x": 0, "source_y": 0, "shape": 1}]
  })";
  out.close();

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->LoadTileset("legacy.json"));
  EXPECT_EQ(loaded->tiles.size(), 1);
  EXPECT_TRUE(loaded->terrains.empty());
}

// How a terrain picks variants decides what a painted region looks like, so it
// is written every time and demanded on read rather than defaulted. A
// definition that predates the field is a definition nobody can interpret.
TEST_F(TilesetManagerTest, TerrainVariantPeriodRoundTrips) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].variant_period = 2;
  // A 2x2 pattern needs all four phases for every mask it covers.
  for (TerrainRule& rule : tileset.terrains[0].rules) {
    rule.variants = {{.tile_id = 1, .weight = 1},
                     {.tile_id = 2, .weight = 1},
                     {.tile_id = 3, .weight = 1},
                     {.tile_id = 1, .weight = 1}};
  }

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(tileset));
  ReloadFromDisk();

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  ASSERT_EQ(loaded->terrains.size(), 1);
  EXPECT_EQ(loaded->terrains[0].variant_period, 2);
}

TEST_F(TilesetManagerTest, TerrainMissingVariantPeriodFailsToLoad) {
  std::string file_path = test_dir_ + "/definitions/tilesets/unversioned.json";
  std::ofstream out(file_path);
  out << R"({
    "id": "unversioned-id",
    "name": "Unversioned",
    "texture_id": "tx",
    "tile_width": 32,
    "tile_height": 32,
    "tiles": [{"id": 1, "name": "Solid", "source_x": 0, "source_y": 0, "shape": 1}],
    "terrains": [{
      "id": 1,
      "name": "Grass",
      "scheme": 0,
      "solid_outside_level": true,
      "rules": [{"mask": 0, "variants": [{"tile_id": 1, "weight": 1}]}]
    }]
  })";
  out.close();

  EXPECT_FALSE(manager_->LoadTileset("unversioned.json").ok());
}

TEST_F(TilesetManagerTest, SaveTileset_PeriodicTerrainMissingAPhase_Fails) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].variant_period = 2;

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("needs 4 variants"));
}

// A tileset with no terrains must not gain a "terrains" key on disk.
TEST_F(TilesetManagerTest, TilesetWithoutTerrainsOmitsTheKey) {
  ASSERT_OK_AND_ASSIGN(std::string id,
                       manager_->CreateTileset(Tileset{
                           .name = "Plain",
                           .texture_id = "tx",
                           .tiles = {Tile{.id = 1, .name = "Solid"}},
                       }));

  std::ifstream in(test_dir_ + "/definitions/tilesets/Plain-" + id + ".json");
  ASSERT_TRUE(in.is_open());
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_THAT(contents, ::testing::Not(HasSubstr("terrains")));
}

TEST_F(TilesetManagerTest, SaveTileset_TerrainReferencingUnknownTile_Fails) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].rules[0].variants[0].tile_id = 99;

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("unknown tile ID 99"));
}

TEST_F(TilesetManagerTest, SaveTileset_TerrainWithDuplicateMask_Fails) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].rules[1].mask = 0;

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("repeats mask 0"));
}

TEST_F(TilesetManagerTest, SaveTileset_TerrainWithEmptyVariants_Fails) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].rules[0].variants.clear();

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("at least one variant"));
}

TEST_F(TilesetManagerTest, SaveTileset_TerrainWithNonPositiveWeight_Fails) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].rules[1].variants[0].weight = 0;

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("weights must be positive"));
}

TEST_F(TilesetManagerTest, TerrainMemberTileIdsSurviveRoundTrip) {
  Tileset tileset = MakeTerrainTileset();
  tileset.tiles.push_back(Tile{.id = 4, .name = "Slope", .shape = TileShape::kSlope45BottomLeft});
  tileset.terrains[0].member_tile_ids = {4};

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(std::move(tileset)));
  ReloadFromDisk();

  ASSERT_OK_AND_ASSIGN(Tileset * loaded, manager_->GetTileset(id));
  ASSERT_EQ(loaded->terrains.size(), 1);
  EXPECT_THAT(loaded->terrains[0].member_tile_ids, ::testing::ElementsAre(4));
}

// A terrain with no hand-placed pieces must not gain the key on disk.
TEST_F(TilesetManagerTest, TerrainWithoutMembersOmitsTheKey) {
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateTileset(MakeTerrainTileset()));

  std::ifstream in(test_dir_ + "/definitions/tilesets/GrassTerrain-" + id + ".json");
  ASSERT_TRUE(in.is_open());
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_THAT(contents, ::testing::Not(HasSubstr("member_tile_ids")));
}

TEST_F(TilesetManagerTest, SaveTileset_TerrainMemberReferencingUnknownTile_Fails) {
  Tileset tileset = MakeTerrainTileset();
  tileset.terrains[0].member_tile_ids = {77};

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("unknown member tile ID 77"));
}

TEST_F(TilesetManagerTest, SaveTileset_TerrainMemberAlsoPainted_Fails) {
  Tileset tileset = MakeTerrainTileset();
  // Tile 1 is already a rule variant.
  tileset.terrains[0].member_tile_ids = {1};

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("both painted and a member"));
}

TEST_F(TilesetManagerTest, SaveTileset_DuplicateTerrainId_Fails) {
  Tileset tileset = MakeTerrainTileset();
  Terrain duplicate = tileset.terrains[0];
  duplicate.name = "Other";
  tileset.terrains.push_back(std::move(duplicate));

  absl::Status status = manager_->CreateTileset(tileset).status();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("Duplicate terrain ID"));
}

}  // namespace
}  // namespace zebes
