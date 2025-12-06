#include "db/sqlite_wrapper.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace zebes {

absl::StatusOr<sqlite3*> SqliteWrapper::Open(const std::string& path) {
  sqlite3* db;
  int rc = sqlite3_open(path.c_str(), &db);
  if (rc != SQLITE_OK) {
    std::string err = sqlite3_errmsg(db);
    sqlite3_close(db);
    return absl::InternalError(
        absl::StrFormat("Failed to open database: %s. Error: %s", path, err));
  }
  return db;
}

absl::Status SqliteWrapper::Close(sqlite3* db) {
  int rc = sqlite3_close(db);
  if (rc != SQLITE_OK) {
    return absl::InternalError(
        absl::StrFormat("Failed to close database. Error: %s", sqlite3_errmsg(db)));
  }
  return absl::OkStatus();
}

absl::Status SqliteWrapper::Execute(sqlite3* db, const std::string& sql,
                                    const std::string& error_message) {
  char* error_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_msg);
  if (rc != SQLITE_OK) {
    std::string err(error_msg ? error_msg : "Unknown error");
    sqlite3_free(error_msg);
    return absl::InternalError(absl::StrFormat("%s SQLite Error: %s", error_message, err));
  }
  return absl::OkStatus();
}

absl::StatusOr<sqlite3_stmt*> SqliteWrapper::Prepare(sqlite3* db, const std::string& query) {
  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return absl::InternalError(
        absl::StrFormat("Failed to prepare statement. SQLite Error: %s", sqlite3_errmsg(db)));
  }
  return stmt;
}

absl::StatusOr<bool> SqliteWrapper::Step(sqlite3_stmt* stmt) {
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    return true;
  }
  if (rc == SQLITE_DONE) {
    return false;
  }
  return absl::InternalError(absl::StrFormat("Failed to execute step. SQLite Error: %d", rc));
}

absl::Status SqliteWrapper::Finalize(sqlite3_stmt* stmt) {
  int rc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK) {
    return absl::InternalError(
        absl::StrFormat("Failed to finalize statement. SQLite Error: %d", rc));
  }
  return absl::OkStatus();
}

int SqliteWrapper::ColumnInt(sqlite3_stmt* stmt, int col) { return sqlite3_column_int(stmt, col); }

std::string SqliteWrapper::ColumnText(sqlite3_stmt* stmt, int col) {
  const unsigned char* text = sqlite3_column_text(stmt, col);
  return text ? reinterpret_cast<const char*>(text) : "";
}

int SqliteWrapper::ColumnType(sqlite3_stmt* stmt, int col) {
  return sqlite3_column_type(stmt, col);
}

int64_t SqliteWrapper::LastInsertRowId(sqlite3* db) { return sqlite3_last_insert_rowid(db); }

}  // namespace zebes
