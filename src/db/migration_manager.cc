#include "db/migration_manager.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "db/sqlite_wrapper.h"

namespace zebes {

namespace {

using DirectoryEntry = std::filesystem::directory_entry;
using DirectoryIterator = std::filesystem::directory_iterator;
using Path = std::filesystem::path;

}  // namespace

absl::StatusOr<int> MigrationManager::Migrate(sqlite3* db, const std::string& db_path,
                                              const std::string& migration_dir) {
  // 1. Check if SchemaMigrations table exists
  bool schema_migrations_exists = false;
  {
    ASSIGN_OR_RETURN(
        sqlite3_stmt * stmt,
        SqliteWrapper::Prepare(
            db, "SELECT name FROM sqlite_master WHERE type='table' AND name='SchemaMigrations'"));
    absl::Cleanup stmt_finalizer =
        absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });
    ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
    if (step_result) {
      schema_migrations_exists = true;
    }
  }

  int current_version = -1;

  if (!schema_migrations_exists) {
    LOG(INFO) << "New database detected. Starting at version -1.";
  } else {
    // Table exists, get version
    ASSIGN_OR_RETURN(sqlite3_stmt * stmt,
                     SqliteWrapper::Prepare(db, "SELECT MAX(version) FROM SchemaMigrations"));

    // Check if we have a row
    absl::Cleanup stmt_finalizer =
        absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });
    ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));

    // If we have a row, get the version
    if (step_result && SqliteWrapper::ColumnType(stmt, 0) != SQLITE_NULL) {
      current_version = SqliteWrapper::ColumnInt(stmt, 0);
    }
  }

  LOG(INFO) << "Current database schema version: " << current_version;

  // 3. Find migration files
  if (!std::filesystem::exists(migration_dir)) {
    return absl::NotFoundError(absl::StrFormat("Migration directory not found: %s", migration_dir));
  }

  struct MigrationFile {
    int version = 0;
    Path path;
  };
  std::vector<MigrationFile> migrations;

  for (const DirectoryEntry& entry : DirectoryIterator(migration_dir)) {
    // ... existing logic to find migrations ...
    if (!entry.is_regular_file() || entry.path().extension() != ".sql") {
      continue;
    }

    std::string filename = entry.path().filename().string();
    // Format: 001_name.sql
    size_t underscore_pos = filename.find('_');
    std::string_view version_str = filename;
    if (underscore_pos != std::string::npos) {
      version_str = std::string_view(filename).substr(0, underscore_pos);
    }

    int version = 0;
    auto result =
        std::from_chars(version_str.data(), version_str.data() + version_str.size(), version);
    if (result.ec != std::errc()) {
      LOG(WARNING) << "Skipping invalid migration filename: " << filename;
      continue;
    }

    if (version > current_version) {
      migrations.push_back({version, entry.path()});
    }
  }

  // Backup if there are migrations to apply
  if (!migrations.empty() && std::filesystem::exists(db_path)) {
    std::string backup_path = absl::StrFormat("%s.%d_backup.db", db_path, current_version);
    std::error_code ec;
    std::filesystem::copy_file(db_path, backup_path,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return absl::InternalError(
          absl::StrFormat("Failed to backup database to %s: %s", backup_path, ec.message()));
    }
    LOG(INFO) << "Database backed up to: " << backup_path;
  }
  LOG(INFO) << "Found " << migrations.size() << " migrations to apply.";

  // Sort by version
  std::sort(migrations.begin(), migrations.end(),
            [](const MigrationFile& a, const MigrationFile& b) { return a.version < b.version; });

  // 4. Apply migrations
  int count = 0;
  for (const MigrationFile& m : migrations) {
    LOG(INFO) << "Applying migration " << m.version << ": " << m.path.filename();

    std::ifstream file(m.path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string sql = buffer.str();

    // Basic transaction support.
    RETURN_IF_ERROR(
        SqliteWrapper::Execute(db, "BEGIN TRANSACTION", "Failed to begin transaction."));

    absl::Status status = SqliteWrapper::Execute(
        db, sql, absl::StrFormat("Failed to apply migration %d.", m.version));
    if (!status.ok()) {
      if (!SqliteWrapper::Execute(db, "ROLLBACK", "Failed to rollback.").ok()) {
        LOG(ERROR) << "Failed to rollback transaction during migration failure.";
      }
      return status;
    }

    // Record migration
    std::string record_sql =
        absl::StrFormat("INSERT INTO SchemaMigrations (version) VALUES (%d)", m.version);
    status = SqliteWrapper::Execute(db, record_sql,
                                    absl::StrFormat("Failed to record migration %d.", m.version));
    if (!status.ok()) {
      if (!SqliteWrapper::Execute(db, "ROLLBACK", "Failed to rollback.").ok()) {
        LOG(ERROR) << "Failed to rollback transaction during migration record "
                      "failure.";
      }
      return status;
    }

    RETURN_IF_ERROR(SqliteWrapper::Execute(db, "COMMIT", "Failed to commit transaction."));
    count++;
  }

  if (count > 0) {
    LOG(INFO) << "Successfully applied " << count << " migrations.";
  } else {
    LOG(INFO) << "Database is up to date.";
  }

  return count;
}

}  // namespace zebes
