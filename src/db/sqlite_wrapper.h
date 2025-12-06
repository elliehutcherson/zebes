#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sqlite3.h"

namespace zebes {

class SqliteWrapper {
 public:
  static absl::StatusOr<sqlite3*> Open(const std::string& path);
  static absl::Status Close(sqlite3* db);

  static absl::Status Execute(sqlite3* db, const std::string& sql,
                              const std::string& error_message);

  static absl::StatusOr<sqlite3_stmt*> Prepare(sqlite3* db, const std::string& query);

  // Returns true if a row is available, false if done. Returns error otherwise.
  static absl::StatusOr<bool> Step(sqlite3_stmt* stmt);

  static absl::Status Finalize(sqlite3_stmt* stmt);

  static int ColumnInt(sqlite3_stmt* stmt, int col);
  static std::string ColumnText(sqlite3_stmt* stmt, int col);
  static int ColumnType(sqlite3_stmt* stmt, int col);

  static int64_t LastInsertRowId(sqlite3* db);
};

}  // namespace zebes
