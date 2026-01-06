#include "db/db.h"


#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "common/status_macros.h"
#include "db/migration_manager.h"
#include "db/sqlite_wrapper.h"
#include "sqlite3.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Db>> Db::Create(const Options& options) {
  // Check that the database can be opened.
  LOG(INFO) << "Attempting to open database at: " << options.db_path;
  // Create the Db object first, which will open the database.
  std::unique_ptr<Db> wrapper(new Db(options));
  ASSIGN_OR_RETURN(sqlite3 * db, wrapper->OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Perform migration
  if (!options.migration_path.empty()) {
    RETURN_IF_ERROR(
        MigrationManager::Migrate(db, options.db_path, options.migration_path).status());
  } else {
    LOG(WARNING) << "No migration path provided, skipping migrations.";
  }

  return wrapper;
}

Db::Db(const Options& options) : db_path_(options.db_path) {}

absl::StatusOr<sqlite3*> Db::OpenDb() { return SqliteWrapper::Open(db_path_); }

absl::StatusOr<std::vector<Db::AppliedMigration>> Db::GetAppliedMigrations() {
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Check if table exists
  {
    const std::string check_query =
        "SELECT name FROM sqlite_master WHERE type='table' AND name='SchemaMigrations'";
    ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, check_query));
    absl::Cleanup stmt_finalizer =
        absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });
    ASSIGN_OR_RETURN(bool exists, SqliteWrapper::Step(stmt));
    if (!exists) {
      // Table doesn't exist, return empty list
      return std::vector<Db::AppliedMigration>();
    }
  }

  const std::string query = "SELECT version, applied_at FROM SchemaMigrations ORDER BY version ASC";
  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  std::vector<Db::AppliedMigration> migrations;
  while (true) {
    ASSIGN_OR_RETURN(bool step, SqliteWrapper::Step(stmt));
    if (!step) break;

    Db::AppliedMigration m;
    m.version = SqliteWrapper::ColumnInt(stmt, 0);
    // applied_at might be text or something else, assuming text based on schema (TIMESTAMP usually
    // text in SQLite)
    m.applied_at = SqliteWrapper::ColumnText(stmt, 1);
    migrations.push_back(m);
  }
  return migrations;
}

}  // namespace zebes