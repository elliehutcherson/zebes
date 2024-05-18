#include "sprite_manager.h"

#include <_types/_uint16_t.h>
#include <cstddef>
#include <iostream>
#include <vector>
#include <sqlite3.h>

#include "SDL_image.h"
#include "SDL_render.h"

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "config.h"
#include "sprite.h"
#include "camera.h"
#include "object.h"

namespace zebes {

SpriteManager::SpriteManager(
  const GameConfig* config, SDL_Renderer* renderer, Camera* camera) : 
  config_(config), renderer_(renderer), camera_(camera) {
  return;
}

absl::Status SpriteManager::Init() {
  sqlite3* db;
  int return_code = sqlite3_open(kZebesDatabasePath.c_str(), &db);
  if (return_code != SQLITE_OK) return absl::AbortedError("Failed to open database.");

  // Initialize valid sprites.
  std::string query =  "SELECT * FROM SpriteConfig WHERE type > 0 AND type < 106";

  std::cout << query << std::endl;

  sqlite3_stmt* stmt;
  return_code = sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    // Handle error
    sqlite3_close(db);
    return absl::AbortedError(
      absl::StrFormat("Failed to pull sprites from SpriteConfig table, return_code: %d", return_code));
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SpriteConfig sprite_config;
    sprite_config.type = static_cast<SpriteType>(sqlite3_column_int(stmt, 1));
    sprite_config.texture_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    sprite_config.ticks_per_sprite = sqlite3_column_int(stmt, 4);

    absl::StatusOr<std::vector<SubSprite>> maybe_subsprites = GetSubSprites(sprite_config.type);
    if (!maybe_subsprites.ok()) return maybe_subsprites.status();
    sprite_config.sub_sprites = *maybe_subsprites;
    
    absl::Status result = InitializeSprite(sprite_config);
    if (!result.ok()) return result;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return absl::OkStatus();
}

absl::StatusOr<std::vector<SubSprite>> SpriteManager::GetSubSprites(SpriteType type) {
  std::vector<SubSprite> sub_sprites;

  // Open the database
  sqlite3* db;
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
    "INNER JOIN SpriteConfig ON SubSpriteConfig.sprite_config_id = SpriteConfig.id "
    "WHERE SpriteConfig.type = %d", static_cast<int>(type));
  sqlite3_stmt* stmt;
  return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  std::cout << __func__ << ": " << sqlite3_column_count(stmt) << std::endl;
  std::cout << __func__ << ": " <<  absl::StrFormat("type = %d", static_cast<int>(type)) << std::endl;

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
      sub_sprite.texture_x, 
      sub_sprite.texture_y,
      sub_sprite.texture_w,
      sub_sprite.texture_h, 
      sub_sprite.texture_offset_x, 
      sub_sprite.texture_offset_y,
      sub_sprite.render_w,
      sub_sprite.render_h,
      sub_sprite.render_offset_x,
      sub_sprite.render_offset_y);
    std::cout << test_output << std::endl;

    sub_sprites.push_back(sub_sprite);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  return sub_sprites;
}

absl::Status SpriteManager::InitializeSprite(SpriteConfig sprite_config) {
  if (sprite_config.size() <= 0) {
    return absl::InternalError("Sprite Config has invalid size.");
  }
  SDL_Texture* texture = nullptr;
  auto texture_iter = path_to_texture_.find(sprite_config.texture_path);
  std::cout << __func__ << ": " <<  absl::StrFormat("sprite_config.texture_path = %s", sprite_config.texture_path) << std::endl;
  std::cout << __func__ << ": " <<  absl::StrFormat("sprite_config.type = %d", static_cast<int>(sprite_config.type)) << std::endl;

  if (texture_iter == path_to_texture_.end()) {
    SDL_Surface* tmp_surface = IMG_Load(sprite_config.texture_path.c_str());
    if (tmp_surface == nullptr) 
      return absl::AbortedError("Failed to create tmp_surface.");

    texture = SDL_CreateTextureFromSurface(renderer_, tmp_surface);
    if (texture == nullptr) 
      return absl::AbortedError("Failed to create texture.");
    
    SDL_FreeSurface(tmp_surface);
    path_to_texture_.insert({sprite_config.texture_path, texture});
  }

  sprites_.insert({sprite_config.type, Sprite(sprite_config)});

  return absl::OkStatus();
}

absl::StatusOr<const Sprite*> SpriteManager::GetSprite(SpriteType type) const {
  auto sprite_iter = sprites_.find(type);
  if (sprite_iter == sprites_.end()) {
    return absl::NotFoundError("Sprite not found.");
  }
  return &sprite_iter->second;
}

absl::Status SpriteManager::AddSpriteObject(const SpriteObjectInterface* object) {
  if (object == nullptr) return absl::InvalidArgumentError("SpriteObjectInterface is null.");
  sprite_objects_.push_back(object);
  return absl::OkStatus();
}

void SpriteManager::Render() {
  for (const SpriteObjectInterface* sprite_object : sprite_objects_) {
    Sprite& sprite = sprites_.at(sprite_object->GetActiveSpriteProfile()->type);
    const SpriteConfig* sprite_config = sprite.GetConfig();
    int sprite_index = 0;
    if (sprite_config->ticks_per_sprite > 0) {
      sprite_index =  sprite_object->GetActiveSpriteTicks() / sprite.GetConfig()->ticks_per_sprite;
    }
    const SubSprite* sub_sprite = sprite.GetSubSprite(sprite_index);

    SDL_Texture* texture = path_to_texture_[sprite_config->texture_path];
    SDL_Rect dst_rect {
      .x = sub_sprite->render_offset_x + sprite_object->polygon()->x_min_floor(),
      .y = sprite_object->polygon()->y_max_floor() - sub_sprite->render_h,
      .w = sub_sprite->render_w,
      .h = sub_sprite->render_h,
    };
    const SDL_Rect* src_rect = sprite.GetSource(sprite_index);
    
    camera_->Render(texture, sprite.GetSource(sprite_index), &dst_rect);
    const std::vector<Point>& vertices = *sprite_object->polygon()->vertices();
    camera_->RenderLines(*sprite_object->polygon()->vertices(), DrawColor::kColorTile, false);
  }
}

}  // namespace zebes
