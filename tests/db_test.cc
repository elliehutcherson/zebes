#include "db/db.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

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

}  // namespace
}  // namespace zebes
