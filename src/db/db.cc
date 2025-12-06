#include "db/db.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "common/status_macros.h"
#include "objects/sprite_interface.h"
#include "sqlite3.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<Db>> Db::Create(const Options& options) {
  // Check that the database can be opened.
  sqlite3* db;
  LOG(INFO) << "Attempting to open database at: " << options.db_path;
  int return_code = sqlite3_open(options.db_path.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to open database.");
  }
  sqlite3_close(db);

  return std::unique_ptr<Db>(new Db(options));
}

Db::Db(const Options& options) : db_path_(options.db_path) {}

absl::StatusOr<sqlite3*> Db::OpenDb() {
  sqlite3* db;
  int return_code = sqlite3_open(db_path_.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to open database.");
  }

  return db;
}

absl::StatusOr<uint16_t> Db::InsertTexture(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string statement =
      absl::StrFormat("INSERT INTO TextureConfig(texture_path) VALUES('%s')", path);

  char* err_msg = nullptr;
  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, &err_msg);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    std::string error_str = "SQL execution failed: ";
    if (err_msg != nullptr) {
      error_str = absl::StrCat(error_str, err_msg);
      sqlite3_free(err_msg);
    }

    return absl::AbortedError(error_str);
  }

  uint16_t texture_id = sqlite3_last_insert_rowid(db);
  sqlite3_close(db);
  return texture_id;
}

absl::Status Db::DeleteTexture(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the statement
  const std::string statement =
      absl::StrFormat("DELETE FROM TextureConfig WHERE texture_path = '%s'", path);

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to delete texture.");
  }

  sqlite3_close(db);
  return absl::OkStatus();
}

absl::StatusOr<bool> Db::TextureExists(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string query =
      absl::StrFormat("SELECT COUNT(*) FROM TextureConfig WHERE texture_path = '%s'", path);

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }

  // Fetch the result
  bool exists = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    exists = sqlite3_column_int(stmt, 0) > 0;
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return exists;
}

absl::StatusOr<std::vector<std::string>> Db::GetAllTexturePaths() {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string query = "SELECT texture_path FROM TextureConfig";

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  std::vector<std::string> texture_paths;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* texture_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    texture_paths.emplace_back(texture_path);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return texture_paths;
}

absl::StatusOr<std::vector<Texture>> Db::GetAllTextures() {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string query = "SELECT id, texture_path FROM TextureConfig";

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  std::vector<Texture> textures;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    Texture texture;
    texture.id = sqlite3_column_int(stmt, 0);
    texture.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    textures.push_back(texture);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return textures;
}

absl::StatusOr<int> Db::NumSpritesWithTexture(const std::string& path) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string query =
      absl::StrFormat("SELECT COUNT(*) FROM SpriteConfig WHERE texture_path = '%s'", path);

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  // Fetch the result
  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return count;
}

absl::StatusOr<uint16_t> Db::InsertSprite(const SpriteConfig& sprite_config) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string statement = absl::StrFormat(
      "INSERT INTO SpriteConfig(type, type_name, texture_path, "
      "ticks_per_sprite "
      "VALUES(%d, '%s', '%s', %d)",
      sprite_config.type, sprite_config.type_name, sprite_config.texture_path,
      sprite_config.ticks_per_sprite);

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(db);

  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to insert sprite.");
  }

  return sqlite3_last_insert_rowid(db);
}

absl::Status Db::DeleteSprite(uint16_t sprite_id) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the statement to delete all subsprites
  std::string statement = absl::StrFormat(
      "DELETE FROM SpriteFrameConfig WHERE sprite_config_id = %d", static_cast<int>(sprite_id));

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to delete sprite.");
  }

  // Prepare the statement to delete the sprite
  statement =
      absl::StrFormat("DELETE FROM SpriteConfig WHERE id = %d", static_cast<int>(sprite_id));

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
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string query = absl::StrFormat(
      "SELECT id, type, type_name, texture_path, ticks_per_sprite "
      "FROM SpriteConfig WHERE id = %d",
      static_cast<int>(sprite_id));

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  // Fetch the sprite
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    sprite_config.id = sqlite3_column_int(stmt, 0);
    sprite_config.type = sqlite3_column_int(stmt, 1);
    sprite_config.type_name = sqlite3_column_int(stmt, 2);
    sprite_config.texture_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    sprite_config.ticks_per_sprite = sqlite3_column_int(stmt, 4);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sprite_config;
}

absl::StatusOr<std::vector<SpriteConfig>> Db::GetAllSprites() {
  std::vector<SpriteConfig> sprite_configs;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string query =
      "SELECT id, type, type_name, texture_path, ticks_per_sprite "
      "FROM SpriteConfig";

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  // Fetch the sprite
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SpriteConfig sprite_config;
    sprite_config.id = sqlite3_column_int(stmt, 0);
    sprite_config.type = sqlite3_column_int(stmt, 1);
    sprite_config.type_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    sprite_config.texture_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    sprite_config.ticks_per_sprite = sqlite3_column_int(stmt, 4);

    sprite_configs.push_back(sprite_config);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sprite_configs;
};

absl::StatusOr<uint16_t> Db::InsertSpriteFrame(uint16_t sprite_id,
                                               const SpriteFrame& sprite_frame) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

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

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to insert sprite frame.");
  }

  uint16_t sprite_frame_id = sqlite3_last_insert_rowid(db);
  sqlite3_close(db);

  return sprite_frame_id;
}

absl::Status Db::DeleteSpriteFrame(uint16_t sprite_frame_id) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the statement to delete the sprite frame
  const std::string statement = absl::StrFormat("DELETE FROM SpriteFrameConfig WHERE id = %d",
                                                static_cast<int>(sprite_frame_id));

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(db);

  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to delete sprite frame.");
  }

  return absl::OkStatus();
}

absl::StatusOr<SpriteFrame> Db::GetSpriteFrame(uint16_t sprite_frame_id) {
  SpriteFrame sprite_frame;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

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

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  // Fetch the sprite frame
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    sprite_frame.texture_x = sqlite3_column_int(stmt, 0);
    sprite_frame.texture_y = sqlite3_column_int(stmt, 1);
    sprite_frame.texture_w = sqlite3_column_int(stmt, 2);
    sprite_frame.texture_h = sqlite3_column_int(stmt, 3);
    sprite_frame.texture_offset_x = sqlite3_column_int(stmt, 4);
    sprite_frame.texture_offset_y = sqlite3_column_int(stmt, 5);
    sprite_frame.render_w = sqlite3_column_int(stmt, 6);
    sprite_frame.render_h = sqlite3_column_int(stmt, 7);
    sprite_frame.render_offset_x = sqlite3_column_int(stmt, 8);
    sprite_frame.render_offset_y = sqlite3_column_int(stmt, 9);
    sprite_frame.index = sqlite3_column_int(stmt, 10);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sprite_frame;
}

absl::StatusOr<std::vector<SpriteFrame>> Db::GetSpriteFrames(uint16_t sprite_id) {
  std::vector<SpriteFrame> sprite_frames;

  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

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

  sqlite3_stmt* stmt;
  int return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  LOG(INFO) << __func__ << ": " << sqlite3_column_count(stmt);
  LOG(INFO) << __func__ << ": " << absl::StrFormat("sprite_id = %d", sprite_id);

  // Fetch the sprite frames
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SpriteFrame sprite_frame;
    sprite_frame.id = sqlite3_column_int(stmt, 0);
    sprite_frame.texture_x = sqlite3_column_int(stmt, 1);
    sprite_frame.texture_y = sqlite3_column_int(stmt, 2);
    sprite_frame.texture_w = sqlite3_column_int(stmt, 3);
    sprite_frame.texture_h = sqlite3_column_int(stmt, 4);
    sprite_frame.texture_offset_x = sqlite3_column_int(stmt, 5);
    sprite_frame.texture_offset_y = sqlite3_column_int(stmt, 6);
    sprite_frame.render_w = sqlite3_column_int(stmt, 7);
    sprite_frame.render_h = sqlite3_column_int(stmt, 8);
    sprite_frame.render_offset_x = sqlite3_column_int(stmt, 9);
    sprite_frame.render_offset_y = sqlite3_column_int(stmt, 10);
    sprite_frame.index = sqlite3_column_int(stmt, 11);

    sprite_frames.push_back(sprite_frame);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sprite_frames;
}

absl::Status Db::UpdateSprite(const SpriteConfig& config) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

  // Prepare the query
  const std::string statement = absl::StrFormat(
      "UPDATE SpriteConfig SET type=%d, type_name='%s', texture_path='%s', "
      "ticks_per_sprite=%d WHERE id=%d",
      config.type, config.type_name.c_str(), config.texture_path.c_str(), config.ticks_per_sprite,
      config.id);

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(db);

  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to update sprite.");
  }

  return absl::OkStatus();
}

absl::Status Db::UpdateSpriteFrame(const SpriteFrame& frame) {
  // Open the database
  ASSIGN_OR_RETURN(sqlite3 * db, OpenDb());

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

  int return_code = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, nullptr);
  sqlite3_close(db);

  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Failed to update sprite frame.");
  }

  return absl::OkStatus();
}

}  // namespace zebes