#pragma once

#include <string>

#include "absl/status/statusor.h"
#include "sqlite3.h"

namespace zebes {

class MigrationManager {
 public:
  // Migrates the database to the latest version found in migration_dir.
  // Returns migration count performed.
  static absl::StatusOr<int> Migrate(sqlite3* db, const std::string& db_path,
                                     const std::string& migration_dir);
};

}  // namespace zebes
