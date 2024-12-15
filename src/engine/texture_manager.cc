#include "texture_manager.h"

#include <sqlite3.h>

#include <memory>
#include <vector>

#include "SDL_image.h"
#include "SDL_rect.h"
#include "SDL_render.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "engine/config.h"

namespace zebes {

absl::StatusOr<std::unique_ptr<TextureManager>> TextureManager::Create(
    const TextureManager::Options &options) {
  std::unique_ptr<TextureManager> texture_manager =
      std::unique_ptr<TextureManager>(new TextureManager(options));
  absl::Status result = texture_manager->Init();

  if (!result.ok()) return result;
  return texture_manager;
}

TextureManager::TextureManager(const Options &options)
    : config_(options.config),
      camera_(options.camera),
      renderer_(options.renderer) {}

absl::Status TextureManager::Init() {
  sqlite3 *db;
  int return_code = sqlite3_open(kZebesDatabasePath.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Unable to open database.");
  }

  // Prepare the query
  std::string query =
      "SELECT "
      "type, "
      "type_name, "
      "texture_path, "
      "ticks_per_sprite "
      "FROM SpriteConfig ";

  sqlite3_stmt *statement;
  return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &statement, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  // Fetch the sub sprites
  while (sqlite3_step(statement) == SQLITE_ROW) {
    TexturePack pack;
    pack.type = sqlite3_column_int(statement, 0);
    pack.name =
        reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
    pack.path =
        reinterpret_cast<const char *>(sqlite3_column_text(statement, 2));
    pack.ticks_per_sprite = sqlite3_column_int(statement, 2);
    texture_packs_[pack.type] = std::move(pack);
  }

  sqlite3_finalize(statement);
  sqlite3_close(db);

  for (auto &it : texture_packs_) {
    int type = it.first;
    TexturePack &pack = it.second;

    if (type <= 0) continue;

    absl::Status result = LoadSdlTextures(it.second);
    if (!result.ok()) return result;

    result = LoadTextures(it.second);
    if (!result.ok()) return result;
  }

  return absl::OkStatus();
}

absl::Status TextureManager::LoadSdlTextures(TexturePack &pack) {
  LOG(INFO) << __func__ << ": "
            << absl::StrFormat("texture_path = %s\n", pack.path)
            << absl::StrFormat("type = %d", pack.type);

  auto it = path_to_texture_.find(pack.path);
  if (it != path_to_texture_.end()) return absl::OkStatus();

  SDL_Surface *tmp_surface = IMG_Load(pack.path.c_str());
  if (tmp_surface == nullptr)
    return absl::AbortedError("Failed to create tmp_surface.");

  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, tmp_surface);
  if (texture == nullptr)
    return absl::AbortedError("Failed to create texture.");

  SDL_FreeSurface(tmp_surface);
  path_to_texture_.insert({pack.path, texture});

  return absl::OkStatus();
}

absl::Status TextureManager::LoadTextures(TexturePack &pack) {
  // Open the database
  sqlite3 *db;
  int return_code = sqlite3_open(kZebesDatabasePath.c_str(), &db);
  if (return_code != SQLITE_OK) {
    return absl::AbortedError("Unable to open database.");
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
      pack.type);
  sqlite3_stmt *statement;
  return_code = sqlite3_prepare_v2(db, query.c_str(), -1, &statement, nullptr);
  if (return_code != SQLITE_OK) {
    sqlite3_close(db);
    return absl::AbortedError("Failed to prepare statement.");
  }

  LOG(INFO) << __func__ << ": " << sqlite3_column_count(statement);
  LOG(INFO) << __func__ << ": " << absl::StrFormat("type = %d", pack.type);

  // Fetch the sub sprites
  while (sqlite3_step(statement) == SQLITE_ROW) {
    Texture texture;
    texture.x = sqlite3_column_int(statement, 0);
    texture.y = sqlite3_column_int(statement, 1);
    texture.w = sqlite3_column_int(statement, 2);
    texture.h = sqlite3_column_int(statement, 3);
    texture.offset_x = sqlite3_column_int(statement, 4);
    texture.offset_y = sqlite3_column_int(statement, 5);
    texture.render_w = sqlite3_column_int(statement, 6);
    texture.render_h = sqlite3_column_int(statement, 7);
    texture.render_offset_x = sqlite3_column_int(statement, 8);
    texture.render_offset_y = sqlite3_column_int(statement, 9);

    pack.textures.push_back(texture);
  }

  // Finalize the statement and close the database
  sqlite3_finalize(statement);
  sqlite3_close(db);

  return absl::OkStatus();
}

absl::Status TextureManager::Render(uint16_t sprite_id, int index,
                                    Point position) {
  auto pack = texture_packs_.find(sprite_id);
  if (pack == texture_packs_.end())
    return absl::InvalidArgumentError("Type not found.");

  if (index >= pack->second.textures.size())
    return absl::InvalidArgumentError("Index out of bounds.");

  Texture &texture = pack->second.textures[index];
  SDL_Texture *sdl_texture = path_to_texture_[pack->second.path];
  if (sdl_texture == nullptr)
    return absl::AbortedError("Failed to find texture.");

  SDL_Rect dst_rect{
      .x = position.x_floor() + texture.render_offset_x,
      .y = position.y_floor() + texture.render_offset_y,
      .w = texture.render_w,
      .h = texture.render_h,
  };

  SDL_Rect src_rect{
      .x = texture.x,
      .y = texture.y,
      .w = texture.w,
      .h = texture.h,
  };

  camera_->Render(sdl_texture, &src_rect, &dst_rect);

  return absl::OkStatus();
}

SDL_Texture *TextureManager::Experiment() {
  static SDL_Texture *target_texture = nullptr;
  if (target_texture != nullptr) return target_texture;

  const TexturePack &pack = texture_packs_[1];
  const Texture &texture = pack.textures.front();
  SDL_Texture *original_texture = path_to_texture_[pack.path];
  SDL_Rect src_rect{
      .x = texture.x,
      .y = texture.y,
      .w = texture.w,
      .h = texture.h,
  };

  target_texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_TARGET, 256, 256);
  SDL_SetRenderTarget(renderer_, target_texture);
  SDL_RenderCopy(renderer_, original_texture, &src_rect, nullptr);
  SDL_SetRenderTarget(renderer_, nullptr);

  return target_texture;
}

}  // namespace zebes
