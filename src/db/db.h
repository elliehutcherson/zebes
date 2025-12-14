#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "db/db_interface.h"
#include "sqlite3.h"

namespace zebes {

class Db : public DbInterface {
 public:
  struct Options {
    const std::string db_path;
    const std::string migration_path;
  };
  static absl::StatusOr<std::unique_ptr<Db>> Create(const Options& options);

  struct AppliedMigration {
    int version;
    std::string applied_at;
  };
  /**
   * @brief Returns list of applied migrations from schema table
   */
  absl::StatusOr<std::vector<AppliedMigration>> GetAppliedMigrations();

 private:
  Db(const Options& options);

  absl::StatusOr<sqlite3*> OpenDb();

  const std::string db_path_;
};

}  // namespace zebes