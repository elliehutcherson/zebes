#include "game/game_level_assets.h"

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "common/status_macros.h"
#include "engine/texture_handle.h"
#include "objects/collider.h"
#include "objects/entity.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/sprite.h"
#include "objects/tileset.h"

namespace zebes {
namespace {

absl::StatusOr<TextureHandle> RequireTextureHandle(Api& api, const std::string& texture_id,
                                                   std::string_view owner) {
  if (texture_id.empty()) {
    return absl::FailedPreconditionError(absl::StrCat(owner, " has no texture ID"));
  }
  ASSIGN_OR_RETURN(const TextureHandle handle, api.GetTextureHandle(texture_id));
  if (!handle) {
    return absl::FailedPreconditionError(absl::StrCat(owner, " texture is not loaded"));
  }
  return handle;
}

absl::Status LoadEntitySprites(Api& api, GameLevelAssets& assets) {
  for (const WorldLayer& layer : assets.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      if (entity.sprite_id.empty() || assets.sprites.contains(entity.sprite_id)) continue;
      ASSIGN_OR_RETURN(Sprite * sprite, api.GetSprite(entity.sprite_id));
      if (sprite == nullptr) {
        return absl::FailedPreconditionError(
            absl::StrCat("entity ", entity_id, " sprite resolved to null"));
      }
      if (sprite->id != entity.sprite_id) {
        return absl::FailedPreconditionError(
            absl::StrCat("entity ", entity_id, " resolved the wrong sprite definition"));
      }
      ASSIGN_OR_RETURN(
          const TextureHandle texture,
          RequireTextureHandle(api, sprite->texture_id, absl::StrCat("sprite ", sprite->id)));
      assets.sprites.emplace(sprite->id, *sprite);
      assets.sprite_textures.emplace(sprite->id, texture);
    }
  }
  return absl::OkStatus();
}

absl::Status LoadEntityColliders(Api& api, GameLevelAssets& assets) {
  for (const WorldLayer& layer : assets.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      if (entity.collider_id.empty() || assets.colliders.contains(entity.collider_id)) continue;
      ASSIGN_OR_RETURN(Collider * collider, api.GetCollider(entity.collider_id));
      if (collider == nullptr) {
        return absl::FailedPreconditionError(
            absl::StrCat("entity ", entity_id, " collider resolved to null"));
      }
      if (collider->id != entity.collider_id) {
        return absl::FailedPreconditionError(
            absl::StrCat("entity ", entity_id, " resolved the wrong collider definition"));
      }
      assets.colliders.emplace(collider->id, *collider);
    }
  }
  return absl::OkStatus();
}

absl::Status LoadParallaxThemes(Api& api, GameLevelAssets& assets) {
  for (const ParallaxZone& zone : assets.level.zones) {
    if (assets.parallax_themes.contains(zone.theme_id)) continue;
    ASSIGN_OR_RETURN(ParallaxTheme * theme, api.GetParallaxTheme(zone.theme_id));
    if (theme == nullptr) {
      return absl::FailedPreconditionError(
          absl::StrCat("parallax zone ", zone.id, " theme resolved to null"));
    }
    if (theme->id != zone.theme_id) {
      return absl::FailedPreconditionError(
          absl::StrCat("parallax zone ", zone.id, " resolved the wrong theme definition"));
    }
    for (const ParallaxLayer& layer : theme->layers) {
      for (const ParallaxElement& element : layer.elements) {
        if (assets.parallax_textures.contains(element.texture_id)) continue;
        ASSIGN_OR_RETURN(const TextureHandle texture,
                         RequireTextureHandle(api, element.texture_id,
                                              absl::StrCat("parallax element ", element.id)));
        assets.parallax_textures.emplace(element.texture_id, texture);
      }
    }
    assets.parallax_themes.emplace(theme->id, *theme);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<GameLevelAssets> LoadGameLevelAssets(Api& api, std::string_view level_id) {
  if (level_id.empty()) return absl::InvalidArgumentError("Game level ID is empty");

  const std::string requested_level_id(level_id);
  ASSIGN_OR_RETURN(Level * level, api.GetLevel(requested_level_id));
  if (level == nullptr) return absl::FailedPreconditionError("Game level resolved to null");
  if (level->id != requested_level_id) {
    return absl::FailedPreconditionError("Game level lookup resolved the wrong definition");
  }
  if (level->tileset_id.empty()) {
    return absl::FailedPreconditionError("Game level has no tileset");
  }

  ASSIGN_OR_RETURN(Tileset * tileset, api.GetTileset(level->tileset_id));
  if (tileset == nullptr) return absl::FailedPreconditionError("Game tileset resolved to null");
  if (tileset->id != level->tileset_id) {
    return absl::FailedPreconditionError("Game tileset lookup resolved the wrong definition");
  }
  ASSIGN_OR_RETURN(const TextureHandle tileset_texture,
                   RequireTextureHandle(api, tileset->texture_id, "game tileset"));

  GameLevelAssets assets{
      .level = *level,
      .tileset = *tileset,
      .tileset_texture = tileset_texture,
  };
  RETURN_IF_ERROR(LoadEntitySprites(api, assets));
  RETURN_IF_ERROR(LoadEntityColliders(api, assets));
  RETURN_IF_ERROR(LoadParallaxThemes(api, assets));
  return assets;
}

}  // namespace zebes
