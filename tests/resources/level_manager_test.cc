#include "resources/level_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "macros.h"

namespace zebes {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;

class LevelManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temporary directory for testing
    std::string test_dir = "test_data/level_manager_test";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir + "/definitions/levels");

    // Levels reference sprites and colliders by ID, so no asset managers are
    // needed to round-trip one.
    ASSERT_OK_AND_ASSIGN(manager_, LevelManager::Create(test_dir));
  }

  void TearDown() override { std::filesystem::remove_all("/tmp/test_level_manager"); }

  std::unique_ptr<LevelManager> manager_;
};

TEST_F(LevelManagerTest, CreateAndGetLevel) {
  Level level{
      .name = "My Level",
  };

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  EXPECT_FALSE(id.empty());

  // Get
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));
  EXPECT_EQ(loaded->name, "My Level");
  EXPECT_EQ(loaded->id, id);
}

TEST_F(LevelManagerTest, SerializationTest) {
  Level level{
      .name = "Complex Level",
      .width = 320,
      .height = 320,
      .spawn_point = {100, 200},
  };

  // Add Parallax
  level.themes[1] = ParallaxTheme{
      .id = 1,
      .name = "My Theme",
      .layers = {ParallaxLayer{
          .name = "My Layer",
          .texture_id = "tex1",
          .scroll_factor = {0.5, 0.5},
          .repeat_x = true,
      }},
  };

  // Add Tile Chunk
  TileChunk chunk;
  chunk.tiles[0] = 1;
  chunk.tiles[1] = 2;
  level.tile_chunks[100] = chunk;

  // Add Entity
  auto entity = std::make_unique<Entity>();
  entity->id = 123;
  entity->transform.position = {10, 20};
  entity->body.drag = {1, 0};
  entity->body.mass = 4.5;
  entity->body.is_static = true;

  level.AddEntity(std::move(*entity));

  // Save/Create
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Reload
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  EXPECT_EQ(loaded->name, "Complex Level");
  EXPECT_EQ(loaded->width, 320);
  EXPECT_EQ(loaded->height, 320);
  EXPECT_EQ(loaded->spawn_point.x, 100);
  EXPECT_EQ(loaded->spawn_point.y, 200);

  ASSERT_TRUE(loaded->themes.contains(1));
  ASSERT_EQ(loaded->themes[1].layers.size(), 1);
  EXPECT_EQ(loaded->themes[1].layers[0].name, "My Layer");
  EXPECT_EQ(loaded->themes[1].layers[0].texture_id, "tex1");
  EXPECT_TRUE(loaded->themes[1].layers[0].repeat_x);

  ASSERT_EQ(loaded->tile_chunks.size(), 1);
  EXPECT_EQ(loaded->tile_chunks[100].tiles[0], 1);

  ASSERT_EQ(loaded->entities.size(), 1);
  const Entity& loaded_entity = loaded->entities.at(123);
  EXPECT_EQ(loaded_entity.id, 123);
  EXPECT_EQ(loaded_entity.transform.position.x, 10);
  EXPECT_EQ(loaded_entity.body.drag.x, 1);
  EXPECT_EQ(loaded_entity.body.mass, 4.5);
  EXPECT_TRUE(loaded_entity.body.is_static);
}

TEST_F(LevelManagerTest, DeleteLevel) {
  Level level{
      .name = "To Delete",
  };
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  ASSERT_TRUE(manager_->DeleteLevel(id).ok());
  EXPECT_FALSE(manager_->GetLevel(id).ok());
}

TEST_F(LevelManagerTest, ValidationTest) {
  Level level{
      .name = "Invalid",
      .width = 17,  // Not multiple of 16
      .height = 16,
  };

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(),
              HasSubstr("Level boundaries must be multiples of tile render size"));
}

TEST_F(LevelManagerTest, CreateLevel_EmptyName_Fails) {
  Level level{
      .name = "",
  };
  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("non-empty name"));
}

TEST_F(LevelManagerTest, CreateLevel_DuplicateName_Fails) {
  Level level1{
      .name = "Unique Level",
  };
  ASSERT_OK(manager_->CreateLevel(std::move(level1)));

  Level level2{
      .name = "Unique Level",
  };
  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level2));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("already exists"));
}

// The level editor holds the Level* it is editing for the whole session, and
// saving happens mid-edit. Replacing the cached unique_ptr freed it underneath.
TEST_F(LevelManagerTest, SavingKeepsPointersHandedOutBeforeIt) {
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(Level{.name = "Stable"}));
  ASSERT_OK_AND_ASSIGN(Level * held, manager_->GetLevel(id));

  Level edited = *held;
  edited.name = "Renamed";
  ASSERT_OK(manager_->SaveLevel(edited));

  ASSERT_OK_AND_ASSIGN(Level * after, manager_->GetLevel(id));
  EXPECT_EQ(after, held) << "the address a caller is still holding must survive a save";
  EXPECT_EQ(held->name, "Renamed");
}

TEST_F(LevelManagerTest, SaveLevel_EmptyName_Fails) {
  Level level{
      .name = "Initial Name",
  };
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  loaded->name = "";  // Invalid

  absl::Status status = manager_->SaveLevel(*loaded);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("cannot be empty"));
}

TEST_F(LevelManagerTest, SaveLevel_DuplicateName_Fails) {
  Level level1{
      .name = "Level 1",
  };
  ASSERT_OK(manager_->CreateLevel(std::move(level1)));

  Level level2{
      .name = "Level 2",
  };
  ASSERT_OK_AND_ASSIGN(std::string id2, manager_->CreateLevel(std::move(level2)));

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id2));

  loaded->name = "Level 1";  // Try to rename to existing

  absl::Status status = manager_->SaveLevel(*loaded);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("already taken"));
}

// Every authored field of a layer, round-tripped through disk. Layers belong to
// a theme, which is the only place they have ever been drawn from.
TEST_F(LevelManagerTest, ParallaxLayerPersistence) {
  Level level{
      .name = "Parallax Persistence Level",
  };

  ParallaxLayer layer1{
      .name = "Layer 1",
      .texture_id = "sky_tex",
      .scroll_factor = {0.1, 0.1},
      .offset = {100.0, 200.0},
      .base_scale = 2.5f,
      .repeat_x = true,
      .repeat_y = true,
  };

  ParallaxLayer layer2{
      .name = "Layer 2",
      .texture_id = "mountains_tex",
      .scroll_factor = {0.5, 0.2},
      .offset = {-50.0, 75.0},
      .base_scale = 0.75f,
      .repeat_x = false,
      .repeat_y = false,
  };

  level.themes[1] = ParallaxTheme{.id = 1, .name = "Sky", .layers = {layer1, layer2}};

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Force reload from disk
  manager_ = nullptr;
  ASSERT_OK_AND_ASSIGN(auto new_manager,
                       LevelManager::Create("test_data/level_manager_test"));
  manager_ = std::move(new_manager);
  ASSERT_OK(manager_->LoadAllLevels());

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_TRUE(loaded->themes.contains(1));
  ASSERT_EQ(loaded->themes[1].layers.size(), 2);

  const ParallaxLayer& l1 = loaded->themes[1].layers[0];
  EXPECT_EQ(l1.name, "Layer 1");
  EXPECT_EQ(l1.texture_id, "sky_tex");
  EXPECT_EQ(l1.scroll_factor.x, 0.1);
  EXPECT_EQ(l1.scroll_factor.y, 0.1);
  EXPECT_EQ(l1.offset.x, 100.0);
  EXPECT_EQ(l1.offset.y, 200.0);
  EXPECT_TRUE(l1.repeat_x);
  EXPECT_TRUE(l1.repeat_y);
  EXPECT_FLOAT_EQ(l1.base_scale, 2.5f);

  const ParallaxLayer& l2 = loaded->themes[1].layers[1];
  EXPECT_EQ(l2.name, "Layer 2");
  EXPECT_EQ(l2.texture_id, "mountains_tex");
  EXPECT_EQ(l2.scroll_factor.x, 0.5);
  EXPECT_EQ(l2.scroll_factor.y, 0.2);
  EXPECT_EQ(l2.offset.x, -50.0);
  EXPECT_EQ(l2.offset.y, 75.0);
  EXPECT_FALSE(l2.repeat_x);
  EXPECT_FALSE(l2.repeat_y);
  EXPECT_FLOAT_EQ(l2.base_scale, 0.75f);
}

TEST_F(LevelManagerTest, ZonesAndThemesPersistence) {
  Level level{
      .name = "Theme Level",
      .width = 320,
      .height = 320,
  };

  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Forest";
  ParallaxLayer layer;
  layer.name = "Trees";
  layer.texture_id = "tex_trees";
  layer.base_scale = 1.5f;
  theme.layers.push_back(layer);
  level.themes[1] = theme;

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Forest Zone";
  zone.theme_id = 1;
  zone.min_point = {0, 0};
  zone.max_point = {100, 100};
  zone.fade_length = {10, 10};
  level.zones.push_back(zone);

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Reload
  manager_ = nullptr;
  ASSERT_OK_AND_ASSIGN(auto new_manager,
                       LevelManager::Create("test_data/level_manager_test"));
  manager_ = std::move(new_manager);
  ASSERT_OK(manager_->LoadAllLevels());

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->themes.size(), 1);
  ASSERT_TRUE(loaded->themes.contains(1));
  EXPECT_EQ(loaded->themes[1].layers.size(), 1);
  EXPECT_EQ(loaded->themes[1].layers[0].name, "Trees");
  EXPECT_FLOAT_EQ(loaded->themes[1].layers[0].base_scale, 1.5f);

  ASSERT_EQ(loaded->zones.size(), 1);
  EXPECT_EQ(loaded->zones[0].id, 0);
  EXPECT_EQ(loaded->zones[0].name, "Forest Zone");
  EXPECT_EQ(loaded->zones[0].theme_id, 1);
  EXPECT_EQ(loaded->zones[0].min_point.x, 0);
  EXPECT_EQ(loaded->zones[0].max_point.x, 100);
}

TEST_F(LevelManagerTest, ThemeLayerOffsetPersistence) {
  Level level{
      .name = "Theme Layer Offset Level",
      .width = 320,
      .height = 320,
  };

  ParallaxLayer layer{
      .name = "Trees",
      .texture_id = "tex_trees",
      .offset = {250.0, -100.0},
  };
  ParallaxTheme theme{
      .id = 1,
      .name = "Forest",
      .layers = {layer},
  };
  level.themes[1] = theme;

  ParallaxZone zone{
      .id = 0,
      .name = "Forest Zone",
      .theme_id = 1,
      .min_point = {0, 0},
      .max_point = {100, 100},
      .fade_length = {10, 10},
  };
  level.zones.push_back(zone);

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Force reload from disk
  manager_ = nullptr;
  ASSERT_OK_AND_ASSIGN(auto new_manager,
                       LevelManager::Create("test_data/level_manager_test"));
  manager_ = std::move(new_manager);
  ASSERT_OK(manager_->LoadAllLevels());

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_TRUE(loaded->themes.contains(1));
  ASSERT_EQ(loaded->themes[1].layers.size(), 1);
  EXPECT_EQ(loaded->themes[1].layers[0].offset.x, 250.0);
  EXPECT_EQ(loaded->themes[1].layers[0].offset.y, -100.0);
}

TEST_F(LevelManagerTest, SaveLevel_EmptyThemeName_Fails) {
  Level level{.name = "Bad Theme"};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "";
  level.themes[1] = theme;

  EXPECT_FALSE(manager_->CreateLevel(std::move(level)).ok());
}

TEST_F(LevelManagerTest, SaveLevel_InvalidThemeId_Fails) {
  Level level{.name = "Invalid Theme ID"};
  ParallaxTheme theme;
  theme.id = -1;
  theme.name = "Valid Name";
  level.themes[-1] = theme;

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("valid non-negative integer ID"));
}

TEST_F(LevelManagerTest, LoadLevel_MissingThemeId_Fails) {
  std::string file_path = "test_data/level_manager_test/definitions/levels/bad_theme.json";
  std::ofstream out(file_path);
  out << R"({
    "id": "123",
    "name": "Bad Theme Level",
    "width": 320,
    "height": 320,
    "themes": [
      {
        "name": "Missing ID Theme"
      }
    ]
  })";
  out.close();

  EXPECT_FALSE(manager_->LoadLevel("bad_theme.json").ok());
}

TEST_F(LevelManagerTest, SaveLevel_ZoneInvalidThemeId_Fails) {
  Level level{.name = "Bad Zone"};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Valid Theme";
  level.themes[1] = theme;
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = 99;
  level.zones.push_back(zone);

  EXPECT_FALSE(manager_->CreateLevel(std::move(level)).ok());
}

TEST_F(LevelManagerTest, SaveLevel_EmptyZoneName_Fails) {
  Level level{.name = "Empty Zone Name"};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Valid Theme";
  level.themes[1] = theme;
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "";  // Invalid
  zone.theme_id = 1;
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Zone name cannot be empty"));
}

TEST_F(LevelManagerTest, SaveLevel_DuplicateZoneId_Fails) {
  Level level{.name = "Duplicate Zone ID", .width = 320, .height = 320};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Valid Theme";
  level.themes[1] = theme;
  ParallaxZone zone1;
  zone1.id = 0;
  zone1.name = "Zone A";
  zone1.theme_id = 1;
  zone1.min_point = {0, 0};
  zone1.max_point = {100, 100};
  ParallaxZone zone2;
  zone2.id = 0;  // Duplicate
  zone2.name = "Zone B";
  zone2.theme_id = 1;
  zone2.min_point = {100, 0};
  zone2.max_point = {200, 100};
  level.zones = {zone1, zone2};

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Duplicate zone ID"));
}

TEST_F(LevelManagerTest, LoadLevel_MissingZoneNameAndId_Fails) {
  std::string file_path = "test_data/level_manager_test/definitions/levels/bad_zone.json";
  std::ofstream out(file_path);
  out << R"({
    "id": "456",
    "name": "Bad Zone Level",
    "width": 320,
    "height": 320,
    "themes": [
      {
        "id": 1,
        "name": "Theme"
      }
    ],
    "zones": [
      {
        "theme_id": 1
      }
    ]
  })";
  out.close();

  EXPECT_FALSE(manager_->LoadLevel("bad_zone.json").ok());
}

TEST_F(LevelManagerTest, SaveLevel_ZoneOutsideBounds_Fails) {
  Level level{.name = "Zone Out Of Bounds", .width = 320, .height = 320};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Valid Theme";
  level.themes[1] = theme;

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = 1;
  zone.min_point = {0, 0};
  zone.max_point = {500, 320};  // max_x exceeds level width
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("extends outside level boundaries"));
}

TEST_F(LevelManagerTest, SaveLevel_ZoneNegativeCoords_Fails) {
  Level level{.name = "Negative Zone", .width = 320, .height = 320};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Valid Theme";
  level.themes[1] = theme;

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = 1;
  zone.min_point = {-10, 0};
  zone.max_point = {100, 100};
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("extends outside level boundaries"));
}

TEST_F(LevelManagerTest, SaveLevel_ZoneInvalidDimensions_Fails) {
  Level level{.name = "Inverted Zone", .width = 320, .height = 320};
  ParallaxTheme theme;
  theme.id = 1;
  theme.name = "Valid Theme";
  level.themes[1] = theme;

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = 1;
  zone.min_point = {100, 0};
  zone.max_point = {50, 100};  // min_x > max_x
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("invalid dimensions"));
}

TEST_F(LevelManagerTest, LoadLevel_ZoneOutsideBounds_Fails) {
  std::string file_path = "test_data/level_manager_test/definitions/levels/zone_oob.json";
  std::ofstream out(file_path);
  out << R"({
    "id": "789",
    "name": "Zone OOB Level",
    "width": 320,
    "height": 320,
    "themes": [
      {
        "id": 1,
        "name": "Theme"
      }
    ],
    "zones": [
      {
        "id": 0,
        "name": "Zone 0",
        "theme_id": 1,
        "min_x": 0,
        "min_y": 0,
        "max_x": 999,
        "max_y": 320
      }
    ]
  })";
  out.close();

  EXPECT_FALSE(manager_->LoadLevel("zone_oob.json").ok());
}

TEST_F(LevelManagerTest, SaveLevel_SpawnPointOutsideBounds_Fails) {
  Level level{
      .name = "Bad Spawn",
      .width = 320,
      .height = 320,
      .spawn_point = {500, 100},
  };

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Spawn point is outside level boundaries"));
}

TEST_F(LevelManagerTest, EntityBlueprintFieldsSurviveRoundTrip) {
  Level level{
      .name = "Blueprint Round Trip",
      .width = 320,
      .height = 320,
  };

  Entity entity;
  entity.id = 42;
  entity.blueprint_id = "test-blueprint-uuid";
  entity.blueprint_state_index = 2;
  entity.transform.position = {64, 128};
  level.AddEntity(std::move(entity));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->entities.size(), 1);
  const Entity& loaded_entity = loaded->entities.at(42);
  EXPECT_EQ(loaded_entity.blueprint_id, "test-blueprint-uuid");
  EXPECT_EQ(loaded_entity.blueprint_state_index, 2);
  EXPECT_EQ(loaded_entity.transform.position.x, 64);
  EXPECT_EQ(loaded_entity.transform.position.y, 128);
}

// Draw order is authored, not derived from position or ID, so it has to survive
// the file the way any other authored property does.
TEST_F(LevelManagerTest, EntityDrawOrderSurvivesRoundTrip) {
  Level level{.name = "Draw Order", .width = 320, .height = 320};

  Entity behind;
  behind.id = 1;
  behind.sort_order = -5;
  level.AddEntity(std::move(behind));

  Entity in_front;
  in_front.id = 2;
  in_front.sort_order = 12;
  level.AddEntity(std::move(in_front));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->entities.size(), 2);
  EXPECT_EQ(loaded->entities.at(1).sort_order, -5);
  EXPECT_EQ(loaded->entities.at(2).sort_order, 12);
}

// --- Definition / runtime boundary -------------------------------------------

// Velocity and acceleration are simulation state. A level file records what was
// authored, not how fast something happened to be moving when it was saved.
TEST_F(LevelManagerTest, SimulationStateIsNotPersisted) {
  Level level{.name = "Motion", .width = 320, .height = 320};
  Entity entity;
  entity.id = 7;
  entity.body.mass = 2.0;
  entity.body.is_static = true;
  level.AddEntity(std::move(entity));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  std::ifstream in("test_data/level_manager_test/definitions/levels/Motion-" + id + ".json");
  ASSERT_TRUE(in.is_open());
  const std::string contents((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

  EXPECT_THAT(contents, ::testing::Not(HasSubstr("\"vx\"")));
  EXPECT_THAT(contents, ::testing::Not(HasSubstr("\"vy\"")));
  EXPECT_THAT(contents, ::testing::Not(HasSubstr("\"ax\"")));
  EXPECT_THAT(contents, ::testing::Not(HasSubstr("\"ay\"")));
  // Animation playback is likewise runtime state and belongs to the animator.
  EXPECT_THAT(contents, ::testing::Not(HasSubstr("\"current_frame_index\"")));
  // Authored properties are still written.
  EXPECT_THAT(contents, HasSubstr("\"mass\""));
  EXPECT_THAT(contents, HasSubstr("\"is_static\""));
}

// Asset references round-trip as IDs, which is what lets a level load without
// the sprite or collider managers.
TEST_F(LevelManagerTest, EntityAssetReferencesRoundTripAsIds) {
  Level level{.name = "Refs", .width = 320, .height = 320};
  Entity entity;
  entity.id = 11;
  entity.sprite_id = "sprite-uuid";
  entity.collider_id = "collider-uuid";
  level.AddEntity(std::move(entity));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->entities.size(), 1);
  EXPECT_EQ(loaded->entities.at(11).sprite_id, "sprite-uuid");
  EXPECT_EQ(loaded->entities.at(11).collider_id, "collider-uuid");
}

// Levels written before the split carry vx/vy/ax/ay and current_frame_index.
// They must keep loading, with those keys ignored rather than restored.
// Velocity, acceleration and animation playback are outputs of the running
// game, not authored content. Documents written before that split carry them,
// and they are dropped on load rather than restored -- a saved level must not
// resurrect how fast something happened to be moving.
TEST_F(LevelManagerTest, SimulationStateInADocumentIsIgnored) {
  const std::string path =
      "test_data/level_manager_test/definitions/levels/Legacy-legacy-id.json";
  std::ofstream out(path);
  out << R"({
    "id": "legacy-id",
    "name": "Legacy",
    "tileset_id": "",
    "width": 320.0,
    "height": 320.0,
    "tile_render_width": 16,
    "tile_render_height": 16,
    "spawn_point": {"x": 0.0, "y": 0.0},
    "entities": [{
      "id": 9,
      "active": true,
      "blueprint_id": "",
      "blueprint_state_index": 0,
      "sort_order": 0,
      "sprite_id": "",
      "collider_id": "",
      "transform": {"x": 32.0, "y": 48.0, "rotation": 0.0},
      "current_frame_index": 4,
      "body": {"vx": 5.0, "vy": -3.0, "ax": 1.0, "ay": 2.0,
               "drag_x": 0.0, "drag_y": 0.0, "mass": 1.5, "is_static": false}
    }],
    "themes": [], "zones": [], "tile_chunks": []
  })";
  out.close();

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->LoadLevel("Legacy-legacy-id.json"));

  ASSERT_EQ(loaded->entities.size(), 1);
  const Entity& entity = loaded->entities.at(9);
  EXPECT_EQ(entity.transform.position.x, 32);
  EXPECT_EQ(entity.body.mass, 1.5);
  EXPECT_FALSE(entity.body.is_static);
}

// A field the writer always emits is a field the reader requires. Substituting
// a default here would reinterpret the level rather than report the problem.
TEST_F(LevelManagerTest, ALevelMissingARequiredFieldIsRefused) {
  const std::string path =
      "test_data/level_manager_test/definitions/levels/Partial-partial-id.json";
  std::ofstream out(path);
  out << R"({
    "id": "partial-id",
    "name": "Partial",
    "width": 320.0,
    "height": 320.0,
    "tile_render_width": 16,
    "tile_render_height": 16,
    "spawn_point": {"x": 0.0, "y": 0.0},
    "entities": [], "themes": [], "zones": [], "tile_chunks": []
  })";
  out.close();

  absl::StatusOr<Level*> loaded = manager_->LoadLevel("Partial-partial-id.json");

  ASSERT_FALSE(loaded.ok());
  EXPECT_THAT(std::string(loaded.status().message()), ::testing::HasSubstr("tileset_id"));
}

}  // namespace
}  // namespace zebes
