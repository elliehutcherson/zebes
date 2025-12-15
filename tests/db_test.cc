#include "db/db.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "nlohmann/json.hpp"  // Added include

namespace zebes {
namespace {

class DbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temporary directory for the test
    std::string temp_dir = std::filesystem::temp_directory_path();
    test_dir_ = temp_dir + "/db_test_" + std::to_string(std::time(nullptr));
    std::filesystem::create_directory(test_dir_);

    db_path_ = test_dir_ + "/test.db";
    migration_path_ = test_dir_ + "/migrations";
    std::filesystem::create_directory(migration_path_);
  }

  void TearDown() override {
    // Clean up
    std::filesystem::remove_all(test_dir_);
  }

  void CreateMigrationFile(const std::string& name, const std::string& content) {
    std::string path = migration_path_ + "/" + name;
    std::ofstream file(path);
    file << content;
    file.close();
  }

  std::string test_dir_;
  std::string db_path_;
  std::string migration_path_;
};

TEST_F(DbTest, FreshDatabaseCreation) {
  // Create migration files
  CreateMigrationFile("000_create_migrations_table.sql",
                      "CREATE TABLE IF NOT EXISTS SchemaMigrations ("
                      "version INTEGER PRIMARY KEY,"
                      "applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                      ");");
  CreateMigrationFile("001_initial.sql", "CREATE TABLE TestTable (id INTEGER PRIMARY KEY);");

  Db::Options options = {
      .db_path = db_path_,
      .migration_path = migration_path_,
  };

  // This should succeed if the migration logic is correct
  auto result = Db::Create(options);
  ASSERT_TRUE(result.ok()) << "Db::Create failed: " << result.status();

  std::unique_ptr<Db> db = std::move(result.value());

  // Verify migrations were applied
  auto migrations_or = db->GetAppliedMigrations();
  ASSERT_TRUE(migrations_or.ok());

  std::vector<Db::AppliedMigration> migrations = migrations_or.value();
  ASSERT_EQ(migrations.size(), 2);
  EXPECT_EQ(migrations[0].version, 0);
  EXPECT_EQ(migrations[1].version, 1);
  EXPECT_EQ(migrations[1].version, 1);
}

TEST_F(DbTest, RealMigrations) {
  // Copy real migrations from assets
  std::string real_migrations_path = "assets/zebes/sql/migrations";
  ASSERT_TRUE(std::filesystem::exists(real_migrations_path))
      << "Assets path not found: " << real_migrations_path;

  for (const auto& entry : std::filesystem::directory_iterator(real_migrations_path)) {
    std::filesystem::copy_file(entry.path(),
                               migration_path_ + "/" + entry.path().filename().string());
  }

  Db::Options options = {
      .db_path = db_path_,
      .migration_path = migration_path_,
  };

  auto result = Db::Create(options);
  ASSERT_TRUE(result.ok()) << "Db::Create failed with real migrations: " << result.status();

  std::unique_ptr<Db> db = std::move(result.value());
  auto migrations_or = db->GetAppliedMigrations();
  ASSERT_TRUE(migrations_or.ok());

  // We expect at least 4 migrations (000, 001, 002, 003, 004)
  // Actually 5 files.
  EXPECT_GE(migrations_or.value().size(), 5);
}

TEST_F(DbTest, BackupVerification) {
  // 1. Create initial DB with version 0
  CreateMigrationFile("000_init.sql",
                      "CREATE TABLE IF NOT EXISTS SchemaMigrations ("
                      "version INTEGER PRIMARY KEY,"
                      "applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                      ");"
                      "CREATE TABLE Foo (id INT);");
  {
    Db::Options options = {.db_path = db_path_, .migration_path = migration_path_};
    auto result = Db::Create(options);
    ASSERT_TRUE(result.ok()) << "First Db::Create failed: " << result.status();
  }

  // 2. Add new migration
  CreateMigrationFile("001_update.sql", "ALTER TABLE Foo ADD COLUMN val INT;");

  // 3. Migrate again - should trigger backup
  {
    Db::Options options = {.db_path = db_path_, .migration_path = migration_path_};
    auto result = Db::Create(options);
    ASSERT_TRUE(result.ok()) << "Second Db::Create failed: " << result.status();
  }

  // 4. Verify backup exists
  std::string backup_path = db_path_ + ".0_backup.db";
  EXPECT_TRUE(std::filesystem::exists(backup_path)) << "Backup file not found: " << backup_path;
}

TEST_F(DbTest, MigrateTextures) {
  // 1. Create DB
  CreateMigrationFile("000_create_migrations_table.sql",
                      "CREATE TABLE IF NOT EXISTS SchemaMigrations ("
                      "version INTEGER PRIMARY KEY,"
                      "applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                      ");");
  CreateMigrationFile("001_initial.sql",
                      "CREATE TABLE IF NOT EXISTS TextureConfig ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "texture_path TEXT NOT NULL UNIQUE"
                      ");");

  Db::Options options = {.db_path = db_path_, .migration_path = migration_path_};
  auto result = Db::Create(options);
  ASSERT_TRUE(result.ok()) << "Db::Create failed: " << result.status();
  std::unique_ptr<Db> db = std::move(result.value());

  // 2. Insert textures
  std::string texture1 = "textures/hero.png";
  std::string texture2 = "textures/enemy.png";

  auto res1 = db->InsertTexture(texture1);
  ASSERT_TRUE(res1.ok());
  auto res2 = db->InsertTexture(texture2);
  ASSERT_TRUE(res2.ok());

  // 3. Migrate
  // NOTE: The implementation hardcodes "assets/zebes/definitions/textures"
  // relative to CWD. For this test to be robust, we should ideally inject
  // the base path, but given constraints, we will check that location.
  // We need to make sure we clean it up or run it in a way that doesn't mess up real assets.
  // Ideally implementation should take a path or we change CWD.
  // Changing CWD is risky for other tests.
  // Let's assume the implementation writes to relative CWD.

  auto migrate_res = db->MigrateTextures();
  ASSERT_TRUE(migrate_res.ok()) << migrate_res.status();
  EXPECT_EQ(migrate_res.value(), 2);

  // 4. Verify files
  std::string base_path = "assets/zebes/definitions/textures";
  EXPECT_TRUE(std::filesystem::exists(base_path + "/hero.json"));
  EXPECT_TRUE(std::filesystem::exists(base_path + "/enemy.json"));

  // Clean up created files
  std::filesystem::remove(base_path + "/hero.json");
  std::filesystem::remove(base_path + "/enemy.json");
  // Don't remove the directory if it existed before, but we can try removing it if empty
  std::filesystem::remove(base_path);
}

TEST_F(DbTest, MigrateSprites) {
  // 1. Create DB and Tables
  CreateMigrationFile("000_create_migrations_table.sql",
                      "CREATE TABLE IF NOT EXISTS SchemaMigrations ("
                      "version INTEGER PRIMARY KEY,"
                      "applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                      ");");
  CreateMigrationFile("001_initial.sql",
                      "CREATE TABLE IF NOT EXISTS TextureConfig ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "texture_path TEXT NOT NULL UNIQUE"
                      ");"
                      "CREATE TABLE IF NOT EXISTS SpriteConfig ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "type INTEGER NOT NULL,"
                      "type_name TEXT NOT NULL,"
                      "texture_path TEXT NOT NULL,"
                      "texture_id INTEGER NOT NULL"
                      ");"
                      "CREATE TABLE IF NOT EXISTS SpriteFrameConfig ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "sprite_config_id INTEGER NOT NULL,"
                      "sprite_frame_index INTEGER NOT NULL,"
                      "texture_x INTEGER NOT NULL,"
                      "texture_y INTEGER NOT NULL,"
                      "texture_w INTEGER NOT NULL,"
                      "texture_h INTEGER NOT NULL,"
                      "texture_offset_x INTEGER DEFAULT 0,"
                      "texture_offset_y INTEGER DEFAULT 0,"
                      "render_w INTEGER NOT NULL,"
                      "render_h INTEGER NOT NULL,"
                      "render_offset_x INTEGER DEFAULT 0,"
                      "render_offset_y INTEGER DEFAULT 0,"
                      "frames_per_cycle INTEGER NOT NULL" /* Simplified */
                      ");");

  Db::Options options = {.db_path = db_path_, .migration_path = migration_path_};
  auto result = Db::Create(options);
  ASSERT_TRUE(result.ok()) << "Db::Create failed: " << result.status();
  std::unique_ptr<Db> db = std::move(result.value());

  // 2. Insert Data
  std::string texture_path = "textures/atlas.png";
  auto tex_res = db->InsertTexture(texture_path);
  ASSERT_TRUE(tex_res.ok());
  uint16_t texture_id = tex_res.value();  // Needed for verification

  // Use public API to insert sprite
  SpriteMeta sprite;
  sprite.type = 1;
  sprite.type_name = "PlayerIdle";
  sprite.texture_path = texture_path;
  sprite.texture_id = tex_res.value();

  auto sprite_res = db->InsertSprite(sprite);
  ASSERT_TRUE(sprite_res.ok()) << sprite_res.status();
  uint16_t sprite_id = sprite_res.value();

  // Insert Frames via public API
  SpriteFrame frame1;
  frame1.index = 0;
  frame1.texture_x = 0;
  frame1.texture_y = 0;
  frame1.texture_w = 32;
  frame1.texture_h = 32;
  frame1.frames_per_cycle = 10;
  // Set required fields to 0 to avoid garbage
  frame1.texture_offset_x = 0;
  frame1.texture_offset_y = 0;
  frame1.render_w = 32;
  frame1.render_h = 32;
  frame1.render_offset_x = 0;
  frame1.render_offset_y = 0;

  auto frame1_res = db->InsertSpriteFrame(sprite_id, frame1);
  ASSERT_TRUE(frame1_res.ok()) << frame1_res.status();

  SpriteFrame frame2;
  frame2.index = 1;
  frame2.texture_x = 32;
  frame2.texture_y = 0;
  frame2.texture_w = 32;
  frame2.texture_h = 32;
  frame2.frames_per_cycle = 20;
  frame2.texture_offset_x = 0;
  frame2.texture_offset_y = 0;
  frame2.render_w = 32;
  frame2.render_h = 32;
  frame2.render_offset_x = 0;
  frame2.render_offset_y = 0;

  auto frame2_res = db->InsertSpriteFrame(sprite_id, frame2);
  ASSERT_TRUE(frame2_res.ok()) << frame2_res.status();

  // 3. Migrate
  auto migrate_res = db->MigrateSprites();
  ASSERT_TRUE(migrate_res.ok()) << migrate_res.status();
  EXPECT_EQ(migrate_res.value(), 1);

  // 4. Verify
  std::string base_path = "assets/zebes/definitions/sprites";
  std::string file_path = base_path + "/PlayerIdle.json";
  EXPECT_TRUE(std::filesystem::exists(file_path));

  std::ifstream f(file_path);
  std::stringstream buffer;
  buffer << f.rdbuf();
  auto j = nlohmann::json::parse(buffer.str());

  std::string name = j["name"];
  int j_texture_id = j["texture_id"];
  EXPECT_EQ(name, std::string("PlayerIdle"));
  EXPECT_EQ(j_texture_id, static_cast<int>(texture_id));
  EXPECT_TRUE(j["frames"].is_array());
  EXPECT_EQ(j["frames"].size(), 2);

  int d1 = j["frames"][0]["duration"];
  int d2 = j["frames"][1]["duration"];
  int x2 = j["frames"][1]["x"];
  EXPECT_EQ(d1, 10);
  EXPECT_EQ(d2, 20);
  EXPECT_EQ(x2, 32);

  // Cleanup
  std::filesystem::remove(file_path);
  std::filesystem::remove(base_path);
}

}  // namespace
}  // namespace zebes
