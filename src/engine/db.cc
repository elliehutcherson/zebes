#include "db.h"

#include <sqlite3.h>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "sprite_interface.h"
#include "status_macros.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Db>> Db::Create(const Options &options) {
  // Check that the database can be opened.
  sqlite3 *db;
  int return_code = sqlite3_open(options.db_path.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to open database.");
  }
  sqlite3_close(db);

  return std::unique_ptr<Db>(new Db(options));
}

Db::Db(const Options &options) : db_path_(options.db_path) {}

absl::StatusOr<sqlite3 *> Db::OpenDb() {
  sqlite3 *db;
  int return_code = sqlite3_open(db_path_.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to open database.");
  }

  return db;
}

absl::StatusOr<uint16_t> Db::InsertSprite(const SpriteConfig &sprite_config) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  std::string statement = absl::StrFormat(
      "INSERT INTO SpriteConfig(type, type_name, texture_path, "
      "ticks_per_sprite "
      "VALUES(%d, '%s', '%s', %d)",
      sprite_config.type, sprite_config.type_name, sprite_config.texture_path,
      sprite_config.ticks_per_sprite);

  int return_code =
      sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(db);

  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to insert sprite.");
  }

  return sqlite3_last_insert_rowid(db);
}

absl::Status Db::DeleteSprite(uint16_t sprite_id) {
  // Open the database
  sqlite3 *db;
  int return_code = sqlite3_open(db_path_.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to open database.");
  }

  // Prepare the statement to delete all subsprites
  std::string statement =
      absl::StrFormat("DELETE FROM SubSpriteConfig WHERE sprite_config_id = %d",
                      static_cast<int>(sprite_id));

  return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to delete sprite.");
  }

  // Prepare the statement to delete the sprite
  statement = absl::StrFormat("DELETE FROM SpriteConfig WHERE id = %d",
                              static_cast<int>(sprite_id));

  return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(db);

  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to delete sprite.");
  }

  return absl::OkStatus();
}

absl::StatusOr<SpriteConfig> Db::GetSprite(uint16_t sprite_id) {
  SpriteConfig sprite_config;

  // Open the database
  sqlite3 *db;
  int return_code = sqlite3_open(db_path_.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to open database.");
  }

  // Prepare the query
  std::string query = absl::StrFormat(
      "SELECT id, type, type_name, texture_path, ticks_per_sprite "
      "FROM SpriteConfig WHERE id = %d",
      static_cast<int>(sprite_id));

  sqlite3_stmt *stmt;
  return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  // Fetch the sprite
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    sprite_config.id = sqlite3_column_int(stmt, 0);
    sprite_config.type = sqlite3_column_int(stmt, 1);
    sprite_config.type_name = sqlite3_column_int(stmt, 2);
    sprite_config.texture_path =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    sprite_config.ticks_per_sprite = sqlite3_column_int(stmt, 4);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sprite_config;
}

absl::StatusOr<std::vector<SubSprite>> Db::GetSubSprites(uint16_t sprite_id) {
  std::vector<SubSprite> sub_sprites;

  // Open the database
  sqlite3 *db;
  int return_code = sqlite3_open(db_path_.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError(
        absl::StrCat("Failed to open database, return_code: ", return_code));
  }

  // Prepare the query
  std::string query = absl::StrFormat(
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
      "render_offset_y "
      "FROM SubSpriteConfig "
      "INNER JOIN SpriteConfig ON "
      "SubSpriteConfig.sprite_config_id = SpriteConfig.id "
      "WHERE SpriteConfig.id = %d",
      sprite_id);

  sqlite3_stmt *stmt;
  return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  LOG(INFO) << __func__ << ": " << sqlite3_column_count(stmt);
  LOG(INFO) << __func__ << ": " << absl::StrFormat("type = %d", sprite_id);

  // Fetch the sub sprites
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SubSprite sub_sprite;
    sub_sprite.texture_x = sqlite3_column_int(stmt, 0);
    sub_sprite.texture_y = sqlite3_column_int(stmt, 1);
    sub_sprite.texture_w = sqlite3_column_int(stmt, 2);
    sub_sprite.texture_h = sqlite3_column_int(stmt, 3);
    sub_sprite.texture_offset_x = sqlite3_column_int(stmt, 4);
    sub_sprite.texture_offset_y = sqlite3_column_int(stmt, 5);
    sub_sprite.render_w = sqlite3_column_int(stmt, 6);
    sub_sprite.render_h = sqlite3_column_int(stmt, 7);
    sub_sprite.render_offset_x = sqlite3_column_int(stmt, 8);
    sub_sprite.render_offset_y = sqlite3_column_int(stmt, 9);

    std::string test_output = absl::StrFormat(
        "sub_sprite.texture_x = %d\n"
        "sub_sprite.texture_y = %d\n"
        "sub_sprite.texture_w = %d\n"
        "sub_sprite.texture_h = %d\n"
        "sub_sprite.texture_offset_x = %d\n"
        "sub_sprite.texture_offset_y = %d\n"
        "sub_sprite.render_w = %d\n"
        "sub_sprite.render_h = %d\n"
        "sub_sprite.render_offset_x = %d\n"
        "sub_sprite.render_offset_y = %d\n",
        sub_sprite.texture_x, sub_sprite.texture_y, sub_sprite.texture_w,
        sub_sprite.texture_h, sub_sprite.texture_offset_x,
        sub_sprite.texture_offset_y, sub_sprite.render_w, sub_sprite.render_h,
        sub_sprite.render_offset_x, sub_sprite.render_offset_y);
    LOG(INFO) << test_output;

    sub_sprites.push_back(sub_sprite);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sub_sprites;
}

}  // namespace zebes