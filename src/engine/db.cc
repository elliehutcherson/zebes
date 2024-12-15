#include "db.h"

namespace zebes {

void Db::InsertSprite(const SpriteConfig &sprite_config) { return; }

void Db::DeleteSprite(int id) { return; }

absl::StatusOr<SpriteConfig> Db::GetSprite(int id) {}

absl::StatusOr<std::vector<SubSprite>> SpriteManager::GetSubSprites(
    SpriteType type) {
  std::vector<SubSprite> sub_sprites;

  // Open the database
  sqlite3 *db;
  int return_code = sqlite3_open(kZebesDatabasePath.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("hello world!");
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
      "WHERE SpriteConfig.type = %d",
      static_cast<int>(type));
  sqlite3_stmt *stmt;
  return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  LOG(INFO) << __func__ << ": " << sqlite3_column_count(stmt);
  LOG(INFO) << __func__ << ": "
            << absl::StrFormat("type = %d", static_cast<int>(type));

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