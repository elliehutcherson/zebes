#include "db/db.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "db/migration_manager.h"
#include "db/sqlite_wrapper.h"
#include "objects/sprite_interface.h"
#include "sqlite3.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Db>> Db::Create(const Options& options) {
  // Check that the database can be opened.
  LOG(INFO) << "Attempting to open database at: " << options.db_path;
  ASSIGN_OR_RETURN(sqlite3 * db, SqliteWrapper::Open(options.db_path));
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Perform migration
  if (!options.migration_path.empty()) {
    absl::StatusOr<int> status = MigrationManager::Migrate(db, options.migration_path);
    if (!status.ok()) {
      return status.status();
    }
  } else {
    LOG(WARNING) << "No migration path provided, skipping migrations.";
  }

  return std::unique_ptr<Db>(new Db(options));
}

Db::Db(const Options& options) : db_path_(options.db_path) {}

absl::StatusOr<sqlite3*> Db::OpenDb() { return SqliteWrapper::Open(db_path_); }

absl::StatusOr<uint16_t> Db::InsertTexture(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string statement =
      absl::StrFormat("INSERT INTO TextureConfig(texture_path) VALUES('%s')", path);

  absl::Status status = SqliteWrapper::Execute(db, statement, "Failed to insert texture.");
  if (!status.ok()) {
    return status;
  }

  uint16_t texture_id = SqliteWrapper::LastInsertRowId(db);
  return texture_id;
}

absl::Status Db::DeleteTexture(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the statement
  const std::string statement =
      absl::StrFormat("DELETE FROM TextureConfig WHERE texture_path = '%s'", path);

  absl::Status status = SqliteWrapper::Execute(db, statement, "Failed to delete texture.");
  return status;
}

absl::StatusOr<bool> Db::TextureExists(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query =
      absl::StrFormat("SELECT COUNT(*) FROM TextureConfig WHERE texture_path = '%s'", path);

  auto stmt_or = SqliteWrapper::Prepare(db, query);
  if (!stmt_or.ok()) {
    return stmt_or.status();
  }
  sqlite3_stmt* stmt = *stmt_or;
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  // Fetch the result
  bool exists = false;
  auto step_or = SqliteWrapper::Step(stmt);
  if (!step_or.ok()) {
    return step_or.status();
  }

  if (*step_or) {
    exists = SqliteWrapper::ColumnInt(stmt, 0) > 0;
  }

  return exists;
}

absl::StatusOr<std::vector<std::string>> Db::GetAllTexturePaths() {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query = "SELECT texture_path FROM TextureConfig";

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  std::vector<std::string> texture_paths;
  while (true) {
    ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
    if (!step_result) break;

    texture_paths.emplace_back(SqliteWrapper::ColumnText(stmt, 0));
  }

  return texture_paths;
}

absl::StatusOr<std::vector<Texture>> Db::GetAllTextures() {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query = "SELECT id, texture_path FROM TextureConfig";

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  std::vector<Texture> textures;
  while (true) {
    ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
    if (!step_result) break;

    Texture texture;
    texture.id = SqliteWrapper::ColumnInt(stmt, 0);
    texture.path = SqliteWrapper::ColumnText(stmt, 1);
    textures.push_back(texture);
  }

  return textures;
}

absl::StatusOr<int> Db::NumSpritesWithTexture(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query =
      absl::StrFormat("SELECT COUNT(*) FROM SpriteConfig WHERE texture_path = '%s'", path);

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  // Fetch the result
  int count = 0;
  ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
  if (step_result) {
    count = SqliteWrapper::ColumnInt(stmt, 0);
  }

  return count;
}

absl::StatusOr<uint16_t> Db::InsertSprite(const SpriteConfig& sprite_config) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string statement = absl::StrFormat(
      "INSERT INTO SpriteConfig(type, type_name, texture_path, "
      "ticks_per_sprite) "
      "VALUES(%d, '%s', '%s', %d)",
      sprite_config.type, sprite_config.type_name, sprite_config.texture_path,
      sprite_config.ticks_per_sprite);

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to insert sprite."));

  uint16_t id = SqliteWrapper::LastInsertRowId(db);

  return id;
}

absl::Status Db::DeleteSprite(uint16_t sprite_id) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the statement to delete all subsprites
  std::string statement = absl::StrFormat(
      "DELETE FROM SpriteFrameConfig WHERE sprite_config_id = %d", static_cast<int>(sprite_id));

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to delete sprite frames."));

  // Prepare the statement to delete the sprite
  statement =
      absl::StrFormat("DELETE FROM SpriteConfig WHERE id = %d", static_cast<int>(sprite_id));

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to delete sprite."));

  return absl::OkStatus();
}

absl::StatusOr<SpriteConfig> Db::GetSprite(uint16_t sprite_id) {
  SpriteConfig sprite_config;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query = absl::StrFormat(
      "SELECT id, type, type_name, texture_path, ticks_per_sprite "
      "FROM SpriteConfig WHERE id = %d",
      static_cast<int>(sprite_id));

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  // Fetch the sprite
  ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
  if (step_result) {
    sprite_config.id = SqliteWrapper::ColumnInt(stmt, 0);
    sprite_config.type = SqliteWrapper::ColumnInt(stmt, 1);
    // Be careful with ColumnText returning std::string vs const char*
    // existing code: sqlite3_column_text returning uchar*, cast to char*.
    // Wrapper uses ColumnText returning std::string.
    // SpriteConfig fields: type_name is std::string? Let's check type definition if needed.
    // Usually std::string assignment works.
    sprite_config.type_name = SqliteWrapper::ColumnText(stmt, 2);
    sprite_config.texture_path = SqliteWrapper::ColumnText(stmt, 3);
    sprite_config.ticks_per_sprite = SqliteWrapper::ColumnInt(stmt, 4);
  }

  return sprite_config;
}

absl::StatusOr<std::vector<SpriteConfig>> Db::GetAllSprites() {
  std::vector<SpriteConfig> sprite_configs;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query =
      "SELECT id, type, type_name, texture_path, ticks_per_sprite "
      "FROM SpriteConfig";

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  // Fetch the sprite
  while (true) {
    ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
    if (!step_result) break;

    SpriteConfig sprite_config;
    sprite_config.id = SqliteWrapper::ColumnInt(stmt, 0);
    sprite_config.type = SqliteWrapper::ColumnInt(stmt, 1);
    sprite_config.type_name = SqliteWrapper::ColumnText(stmt, 2);
    sprite_config.texture_path = SqliteWrapper::ColumnText(stmt, 3);
    sprite_config.ticks_per_sprite = SqliteWrapper::ColumnInt(stmt, 4);

    sprite_configs.push_back(sprite_config);
  }

  return sprite_configs;
};

absl::StatusOr<uint16_t> Db::InsertSpriteFrame(uint16_t sprite_id,
                                               const SpriteFrame& sprite_frame) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string statement = absl::StrFormat(
      "INSERT INTO SpriteFrameConfig(sprite_config_id, sprite_frame_index, texture_x, texture_y, "
      "texture_w, texture_h, texture_offset_x, texture_offset_y, render_w, "
      "render_h, render_offset_x, render_offset_y) "
      "VALUES(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)",
      sprite_id, sprite_frame.index, sprite_frame.texture_x, sprite_frame.texture_y,
      sprite_frame.texture_w, sprite_frame.texture_h, sprite_frame.texture_offset_x,
      sprite_frame.texture_offset_y, sprite_frame.render_w, sprite_frame.render_h,
      sprite_frame.render_offset_x, sprite_frame.render_offset_y);

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to insert sprite frame."));

  uint16_t sprite_frame_id = SqliteWrapper::LastInsertRowId(db);

  return sprite_frame_id;
}

absl::Status Db::DeleteSpriteFrame(uint16_t sprite_frame_id) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the statement to delete the sprite frame
  const std::string statement = absl::StrFormat("DELETE FROM SpriteFrameConfig WHERE id = %d",
                                                static_cast<int>(sprite_frame_id));

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to delete sprite frame."));

  return absl::OkStatus();
}

absl::StatusOr<SpriteFrame> Db::GetSpriteFrame(uint16_t sprite_frame_id) {
  SpriteFrame sprite_frame;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string query = absl::StrFormat(
      "SELECT "
      "texture_x, "
      "texture_y, "
      "texture_w, "
      "texture_h, "
      "texture_offset_x, "
      "texture_offset_y, "
      "render_w, "
      "render_h, "
      "render_offset_x, "
      "render_offset_y, "
      "sprite_frame_index "
      "FROM SpriteFrameConfig WHERE id = %d",
      sprite_frame_id);

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  // Fetch the sprite frame
  ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
  if (step_result) {
    sprite_frame.texture_x = SqliteWrapper::ColumnInt(stmt, 0);
    sprite_frame.texture_y = SqliteWrapper::ColumnInt(stmt, 1);
    sprite_frame.texture_w = SqliteWrapper::ColumnInt(stmt, 2);
    sprite_frame.texture_h = SqliteWrapper::ColumnInt(stmt, 3);
    sprite_frame.texture_offset_x = SqliteWrapper::ColumnInt(stmt, 4);
    sprite_frame.texture_offset_y = SqliteWrapper::ColumnInt(stmt, 5);
    sprite_frame.render_w = SqliteWrapper::ColumnInt(stmt, 6);
    sprite_frame.render_h = SqliteWrapper::ColumnInt(stmt, 7);
    sprite_frame.render_offset_x = SqliteWrapper::ColumnInt(stmt, 8);
    sprite_frame.render_offset_y = SqliteWrapper::ColumnInt(stmt, 9);
    sprite_frame.index = SqliteWrapper::ColumnInt(stmt, 10);
  }

  return sprite_frame;
}

absl::StatusOr<std::vector<SpriteFrame>> Db::GetSpriteFrames(uint16_t sprite_id) {
  std::vector<SpriteFrame> sprite_frames;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  std::string query = absl::StrFormat(
      "SELECT "
      "SpriteFrameConfig.id, "
      "texture_x, "
      "texture_y, "
      "texture_w, "
      "texture_h, "
      "texture_offset_x, "
      "texture_offset_y, "
      "render_w, "
      "render_h, "
      "render_offset_x, "
      "render_offset_y, "
      "sprite_frame_index "
      "FROM SpriteFrameConfig "
      "INNER JOIN SpriteConfig ON "
      "SpriteFrameConfig.sprite_config_id = SpriteConfig.id "
      "WHERE SpriteConfig.id = %d "
      "ORDER BY sprite_frame_index ASC",
      sprite_id);

  ASSIGN_OR_RETURN(sqlite3_stmt * stmt, SqliteWrapper::Prepare(db, query));
  absl::Cleanup stmt_finalizer =
      absl::MakeCleanup([stmt] { LOG_IF_ERROR(SqliteWrapper::Finalize(stmt)); });

  // Fetch the sprite frames
  while (true) {
    ASSIGN_OR_RETURN(bool step_result, SqliteWrapper::Step(stmt));
    if (!step_result) break;

    SpriteFrame sprite_frame;
    sprite_frame.id = SqliteWrapper::ColumnInt(stmt, 0);
    sprite_frame.texture_x = SqliteWrapper::ColumnInt(stmt, 1);
    sprite_frame.texture_y = SqliteWrapper::ColumnInt(stmt, 2);
    sprite_frame.texture_w = SqliteWrapper::ColumnInt(stmt, 3);
    sprite_frame.texture_h = SqliteWrapper::ColumnInt(stmt, 4);
    sprite_frame.texture_offset_x = SqliteWrapper::ColumnInt(stmt, 5);
    sprite_frame.texture_offset_y = SqliteWrapper::ColumnInt(stmt, 6);
    sprite_frame.render_w = SqliteWrapper::ColumnInt(stmt, 7);
    sprite_frame.render_h = SqliteWrapper::ColumnInt(stmt, 8);
    sprite_frame.render_offset_x = SqliteWrapper::ColumnInt(stmt, 9);
    sprite_frame.render_offset_y = SqliteWrapper::ColumnInt(stmt, 10);
    sprite_frame.index = SqliteWrapper::ColumnInt(stmt, 11);

    sprite_frames.push_back(sprite_frame);
  }

  return sprite_frames;
}

absl::Status Db::UpdateSprite(const SpriteConfig& config) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string statement = absl::StrFormat(
      "UPDATE SpriteConfig SET type=%d, type_name='%s', texture_path='%s', "
      "ticks_per_sprite=%d WHERE id=%d",
      config.type, config.type_name.c_str(), config.texture_path.c_str(), config.ticks_per_sprite,
      config.id);

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to update sprite."));

  return absl::OkStatus();
}

absl::Status Db::UpdateSpriteFrame(const SpriteFrame& frame) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());
  absl::Cleanup db_closer = absl::MakeCleanup([db] { LOG_IF_ERROR(SqliteWrapper::Close(db)); });

  // Prepare the query
  const std::string statement = absl::StrFormat(
      "UPDATE SpriteFrameConfig SET "
      "texture_x=%d, texture_y=%d, texture_w=%d, texture_h=%d, "
      "texture_offset_x=%d, texture_offset_y=%d, "
      "render_w=%d, render_h=%d, render_offset_x=%d, render_offset_y=%d, "
      "sprite_frame_index=%d "
      "WHERE id=%d",
      frame.texture_x, frame.texture_y, frame.texture_w, frame.texture_h, frame.texture_offset_x,
      frame.texture_offset_y, frame.render_w, frame.render_h, frame.render_offset_x,
      frame.render_offset_y, frame.index, frame.id);

  RETURN_IF_ERROR(SqliteWrapper::Execute(db, statement, "Failed to update sprite frame."));

  return absl::OkStatus();
}

}  // namespace zebes