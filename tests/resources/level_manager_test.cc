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

  // Add Tile Chunk
  TileChunk chunk;
  chunk.tiles[0] = 1;
  chunk.tiles[1] = 2;
  level.layers.front().tile_chunks[0] = chunk;

  // Add Entity
  auto entity = std::make_unique<Entity>();
  entity->id = 123;
  entity->transform.position = {10, 20};
  entity->body.drag = {1, 0};
  entity->body.mass = 4.5;
  entity->body.is_static = true;

  ASSERT_OK(level.AddEntity(0, std::move(*entity)));

  // Save/Create
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Reload
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  EXPECT_EQ(loaded->name, "Complex Level");
  EXPECT_EQ(loaded->width, 320);
  EXPECT_EQ(loaded->height, 320);
  EXPECT_EQ(loaded->spawn_point.x, 100);
  EXPECT_EQ(loaded->spawn_point.y, 200);

  ASSERT_EQ(loaded->layers.size(), 1);
  EXPECT_EQ(loaded->layers.front().name, "Base");
  ASSERT_EQ(loaded->layers.front().tile_chunks.size(), 1);
  EXPECT_EQ(loaded->layers.front().tile_chunks[0].tiles[0], 1);

  ASSERT_EQ(loaded->layers.front().entities.size(), 1);
  const Entity& loaded_entity = loaded->layers.front().entities.at(123);
  EXPECT_EQ(loaded_entity.id, 123);
  EXPECT_EQ(loaded_entity.transform.position.x, 10);
  EXPECT_EQ(loaded_entity.body.drag.x, 1);
  EXPECT_EQ(loaded_entity.body.mass, 4.5);
  EXPECT_TRUE(loaded_entity.body.is_static);
}

TEST_F(LevelManagerTest, WorldLayerOrderAndOwnershipSurviveRoundTrip) {
  Level level{
      .name = "Layered",
      .width = 320,
      .height = 320,
  };
  level.layers.front().name = "Background";
  level.layers.push_back(WorldLayer{.id = 5, .name = "Foreground"});
  level.layers.front().tile_chunks[0].tiles[0] = 3;
  level.layers.back().tile_chunks[0].tiles[0] = 9;
  ASSERT_OK(level.AddEntity(0, Entity{.id = 2, .sort_order = -1}));
  ASSERT_OK(level.AddEntity(5, Entity{.id = 8, .sort_order = 4}));

  ASSERT_OK_AND_ASSIGN(const std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->layers.size(), 2u);
  EXPECT_EQ(loaded->layers[0].id, 0);
  EXPECT_EQ(loaded->layers[0].name, "Background");
  EXPECT_EQ(loaded->layers[0].tile_chunks.at(0).tiles[0], 3);
  EXPECT_TRUE(loaded->layers[0].entities.contains(2));
  EXPECT_EQ(loaded->layers[1].id, 5);
  EXPECT_EQ(loaded->layers[1].name, "Foreground");
  EXPECT_EQ(loaded->layers[1].tile_chunks.at(0).tiles[0], 9);
  EXPECT_TRUE(loaded->layers[1].entities.contains(8));
}

TEST_F(LevelManagerTest, DeleteLevel) {
  Level level{
      .name = "To Delete",
  };
  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  ASSERT_OK(manager_->DeleteLevel(id));
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

TEST_F(LevelManagerTest, CreateLevelEmptyNameFails) {
  Level level{
      .name = "",
  };
  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("non-empty name"));
}

TEST_F(LevelManagerTest, CreateLevelDuplicateNameFails) {
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

TEST_F(LevelManagerTest, SaveLevelEmptyNameFails) {
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

TEST_F(LevelManagerTest, SaveLevelDuplicateNameFails) {
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

TEST_F(LevelManagerTest, ZoneThemeReferencePersistence) {
  Level level{
      .name = "Theme Level",
      .width = 320,
      .height = 320,
  };

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Forest Zone";
  zone.theme_id = "forest-theme";
  zone.min_point = {0, 0};
  zone.max_point = {100, 100};
  zone.fade_length = {10, 10};
  level.zones.push_back(zone);

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));

  // Reload
  manager_ = nullptr;
  ASSERT_OK_AND_ASSIGN(auto new_manager, LevelManager::Create("test_data/level_manager_test"));
  manager_ = std::move(new_manager);
  ASSERT_OK(manager_->LoadAllLevels());

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->zones.size(), 1);
  EXPECT_EQ(loaded->zones[0].id, 0);
  EXPECT_EQ(loaded->zones[0].name, "Forest Zone");
  EXPECT_EQ(loaded->zones[0].theme_id, "forest-theme");
  EXPECT_EQ(loaded->zones[0].min_point.x, 0);
  EXPECT_EQ(loaded->zones[0].max_point.x, 100);
}

TEST_F(LevelManagerTest, SaveLevelEmptyZoneThemeIdFails) {
  Level level{.name = "Bad Zone"};
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = "";
  level.zones.push_back(zone);

  EXPECT_FALSE(manager_->CreateLevel(std::move(level)).ok());
}

TEST_F(LevelManagerTest, SaveLevelEmptyZoneNameFails) {
  Level level{.name = "Empty Zone Name"};
  ParallaxZone zone;
  zone.id = 0;
  zone.name = "";  // Invalid
  zone.theme_id = "theme";
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Zone name cannot be empty"));
}

TEST_F(LevelManagerTest, SaveLevelDuplicateZoneIdFails) {
  Level level{.name = "Duplicate Zone ID", .width = 320, .height = 320};
  ParallaxZone zone1;
  zone1.id = 0;
  zone1.name = "Zone A";
  zone1.theme_id = "theme";
  zone1.min_point = {0, 0};
  zone1.max_point = {100, 100};
  ParallaxZone zone2;
  zone2.id = 0;  // Duplicate
  zone2.name = "Zone B";
  zone2.theme_id = "theme";
  zone2.min_point = {100, 0};
  zone2.max_point = {200, 100};
  level.zones = {zone1, zone2};

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("Duplicate zone ID"));
}

TEST_F(LevelManagerTest, LoadLevelMissingZoneNameAndIdFails) {
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

TEST_F(LevelManagerTest, LoadLevelRefusesEmbeddedParallaxThemes) {
  const std::string file_path =
      "test_data/level_manager_test/definitions/levels/Embedded-embedded.json";
  std::ofstream out(file_path);
  out << R"({
    "id": "embedded",
    "name": "Embedded",
    "tileset_id": "",
    "width": 320,
    "height": 320,
    "tile_render_width": 16,
    "tile_render_height": 16,
    "spawn_point": {"x": 0, "y": 0},
    "themes": [],
    "zones": [],
    "layers": [{"id": 0, "name": "Base", "tile_chunks": [], "entities": []}]
  })";
  out.close();

  const absl::Status status = manager_->LoadLevel("Embedded-embedded.json").status();
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(status.message(), HasSubstr("migrate_definitions.py"));
}

TEST_F(LevelManagerTest, SaveLevelZoneOutsideBoundsFails) {
  Level level{.name = "Zone Out Of Bounds", .width = 320, .height = 320};

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = "theme";
  zone.min_point = {0, 0};
  zone.max_point = {500, 320};  // max_x exceeds level width
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("extends outside level boundaries"));
}

TEST_F(LevelManagerTest, SaveLevelZoneNegativeCoordsFails) {
  Level level{.name = "Negative Zone", .width = 320, .height = 320};

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = "theme";
  zone.min_point = {-10, 0};
  zone.max_point = {100, 100};
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("extends outside level boundaries"));
}

TEST_F(LevelManagerTest, SaveLevelZoneInvalidDimensionsFails) {
  Level level{.name = "Inverted Zone", .width = 320, .height = 320};

  ParallaxZone zone;
  zone.id = 0;
  zone.name = "Zone 0";
  zone.theme_id = "theme";
  zone.min_point = {100, 0};
  zone.max_point = {50, 100};  // min_x > max_x
  level.zones.push_back(zone);

  absl::StatusOr<std::string> id = manager_->CreateLevel(std::move(level));
  EXPECT_FALSE(id.ok());
  EXPECT_THAT(id.status().message(), HasSubstr("invalid dimensions"));
}

TEST_F(LevelManagerTest, LoadLevelZoneOutsideBoundsFails) {
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

TEST_F(LevelManagerTest, SaveLevelSpawnPointOutsideBoundsFails) {
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
  ASSERT_OK(level.AddEntity(0, std::move(entity)));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->layers.front().entities.size(), 1);
  const Entity& loaded_entity = loaded->layers.front().entities.at(42);
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
  ASSERT_OK(level.AddEntity(0, std::move(behind)));

  Entity in_front;
  in_front.id = 2;
  in_front.sort_order = 12;
  ASSERT_OK(level.AddEntity(0, std::move(in_front)));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->layers.front().entities.size(), 2);
  EXPECT_EQ(loaded->layers.front().entities.at(1).sort_order, -5);
  EXPECT_EQ(loaded->layers.front().entities.at(2).sort_order, 12);
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
  ASSERT_OK(level.AddEntity(0, std::move(entity)));

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
  ASSERT_OK(level.AddEntity(0, std::move(entity)));

  ASSERT_OK_AND_ASSIGN(std::string id, manager_->CreateLevel(std::move(level)));
  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->GetLevel(id));

  ASSERT_EQ(loaded->layers.front().entities.size(), 1);
  EXPECT_EQ(loaded->layers.front().entities.at(11).sprite_id, "sprite-uuid");
  EXPECT_EQ(loaded->layers.front().entities.at(11).collider_id, "collider-uuid");
}

// Levels written before the split carry vx/vy/ax/ay and current_frame_index.
// They must keep loading, with those keys ignored rather than restored.
// Velocity, acceleration and animation playback are outputs of the running
// game, not authored content. Documents written before that split carry them,
// and they are dropped on load rather than restored -- a saved level must not
// resurrect how fast something happened to be moving.
TEST_F(LevelManagerTest, SimulationStateInADocumentIsIgnored) {
  const std::string path = "test_data/level_manager_test/definitions/levels/Legacy-legacy-id.json";
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
    "layers": [{
      "id": 0,
      "name": "Base",
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
      "tile_chunks": []
    }],
    "zones": []
  })";
  out.close();

  ASSERT_OK_AND_ASSIGN(Level * loaded, manager_->LoadLevel("Legacy-legacy-id.json"));

  ASSERT_EQ(loaded->layers.front().entities.size(), 1);
  const Entity& entity = loaded->layers.front().entities.at(9);
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
    "entities": [], "zones": [], "tile_chunks": []
  })";
  out.close();

  absl::StatusOr<Level*> loaded = manager_->LoadLevel("Partial-partial-id.json");

  ASSERT_FALSE(loaded.ok());
  EXPECT_THAT(std::string(loaded.status().message()), ::testing::HasSubstr("tileset_id"));
}

}  // namespace
}  // namespace zebes
