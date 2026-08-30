#include "resources/level_asset_loader.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
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

absl::StatusOr<TextureHandle> RequireTextureHandle(TextureManager& textures,
                                                   const std::string& texture_id,
                                                   std::string_view owner) {
  if (texture_id.empty()) {
    return absl::FailedPreconditionError(absl::StrCat(owner, " has no texture ID"));
  }
  ASSIGN_OR_RETURN(const TextureHandle handle, textures.GetTextureHandle(texture_id));
  if (!handle) {
    return absl::FailedPreconditionError(absl::StrCat(owner, " texture is not loaded"));
  }
  return handle;
}

absl::Status LoadSpriteReference(const LevelAssetLoaderOptions& resources,
                                 LoadedLevelAssets& assets, const std::string& sprite_id,
                                 std::string_view owner) {
  if (sprite_id.empty() || assets.content.sprites.contains(sprite_id)) return absl::OkStatus();

  ASSIGN_OR_RETURN(Sprite * sprite, resources.sprites.GetSprite(sprite_id));
  if (sprite == nullptr) {
    return absl::FailedPreconditionError(absl::StrCat(owner, " sprite resolved to null"));
  }
  if (sprite->id != sprite_id) {
    return absl::FailedPreconditionError(
        absl::StrCat(owner, " resolved the wrong sprite definition"));
  }
  ASSIGN_OR_RETURN(const TextureHandle texture,
                   RequireTextureHandle(resources.textures, sprite->texture_id,
                                        absl::StrCat("sprite ", sprite->id)));
  assets.content.sprites.emplace(sprite->id, *sprite);
  assets.rendering.sprite_textures.emplace(sprite->id, texture);
  return absl::OkStatus();
}

absl::Status LoadColliderReference(const LevelAssetLoaderOptions& resources,
                                   LoadedLevelAssets& assets, const std::string& collider_id,
                                   std::string_view owner) {
  if (collider_id.empty() || assets.content.colliders.contains(collider_id)) {
    return absl::OkStatus();
  }

  ASSIGN_OR_RETURN(Collider * collider, resources.colliders.GetCollider(collider_id));
  if (collider == nullptr) {
    return absl::FailedPreconditionError(absl::StrCat(owner, " collider resolved to null"));
  }
  if (collider->id != collider_id) {
    return absl::FailedPreconditionError(
        absl::StrCat(owner, " resolved the wrong collider definition"));
  }
  assets.content.colliders.emplace(collider->id, *collider);
  return absl::OkStatus();
}

absl::Status LoadEntitySprites(const LevelAssetLoaderOptions& resources,
                               LoadedLevelAssets& assets) {
  for (const WorldLayer& layer : assets.content.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      RETURN_IF_ERROR(LoadSpriteReference(resources, assets, entity.sprite_id,
                                          absl::StrCat("entity ", entity_id)));
    }
  }
  return absl::OkStatus();
}

absl::Status LoadEntityColliders(const LevelAssetLoaderOptions& resources,
                                 LoadedLevelAssets& assets) {
  for (const WorldLayer& layer : assets.content.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      RETURN_IF_ERROR(LoadColliderReference(resources, assets, entity.collider_id,
                                            absl::StrCat("entity ", entity_id)));
    }
  }
  return absl::OkStatus();
}

absl::Status LoadEntityBlueprints(const LevelAssetLoaderOptions& resources,
                                  LoadedLevelAssets& assets) {
  for (const WorldLayer& layer : assets.content.level.layers) {
    for (const auto& [entity_id, entity] : layer.entities) {
      if (entity.blueprint_id.empty()) continue;

      const Blueprint* blueprint = nullptr;
      const auto loaded = assets.content.blueprints.find(entity.blueprint_id);
      if (loaded != assets.content.blueprints.end()) {
        if (loaded->second == nullptr) {
          return absl::FailedPreconditionError(
              absl::StrCat("entity ", entity_id, " blueprint resolved to null"));
        }
        blueprint = loaded->second.get();
      } else {
        ASSIGN_OR_RETURN(Blueprint * resolved,
                         resources.blueprints.GetBlueprint(entity.blueprint_id));
        if (resolved == nullptr) {
          return absl::FailedPreconditionError(
              absl::StrCat("entity ", entity_id, " blueprint resolved to null"));
        }
        if (resolved->id != entity.blueprint_id) {
          return absl::FailedPreconditionError(
              absl::StrCat("entity ", entity_id, " resolved the wrong blueprint definition"));
        }
        auto definition = std::make_unique<Blueprint>(*resolved);
        blueprint = definition.get();
        assets.content.blueprints.emplace(resolved->id, std::move(definition));
      }

      if (blueprint->states.empty()) {
        return absl::FailedPreconditionError(
            absl::StrCat("entity ", entity_id, " blueprint has no states"));
      }
      const std::optional<int> selected_state_index =
          blueprint->state_index(entity.blueprint_state_key);
      if (!selected_state_index.has_value()) {
        return absl::FailedPreconditionError(absl::StrCat("entity ", entity_id,
                                                          " blueprint has no state key '",
                                                          entity.blueprint_state_key, "'"));
      }

      const Blueprint::State& selected_state = blueprint->states[*selected_state_index];
      if (selected_state.sprite_id != entity.sprite_id ||
          selected_state.collider_id != entity.collider_id) {
        return absl::FailedPreconditionError(absl::StrCat(
            "entity ", entity_id, " does not match its selected blueprint state assets"));
      }

      absl::flat_hash_set<std::string> state_keys;
      for (const Blueprint::State& state : blueprint->states) {
        if (!IsValidBlueprintStateKey(state.key)) {
          return absl::FailedPreconditionError(
              absl::StrCat("blueprint ", blueprint->id, " has an invalid state key"));
        }
        if (!state_keys.insert(state.key).second) {
          return absl::FailedPreconditionError(
              absl::StrCat("blueprint ", blueprint->id, " repeats state key '", state.key, "'"));
        }
        if (state.name.empty()) {
          return absl::FailedPreconditionError(
              absl::StrCat("blueprint ", blueprint->id, " has a state without a name"));
        }
        if (!IsValidBlueprintPlacementMode(state.placement_mode)) {
          return absl::FailedPreconditionError(
              absl::StrCat("blueprint ", blueprint->id, " has an invalid placement mode"));
        }
        const std::string owner = absl::StrCat("blueprint ", blueprint->id, " state ", state.key);
        RETURN_IF_ERROR(LoadSpriteReference(resources, assets, state.sprite_id, owner));
        RETURN_IF_ERROR(LoadColliderReference(resources, assets, state.collider_id, owner));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status LoadParallaxThemes(const LevelAssetLoaderOptions& resources,
                                LoadedLevelAssets& assets) {
  for (const ParallaxZone& zone : assets.content.level.zones) {
    if (assets.content.parallax_themes.contains(zone.theme_id)) continue;
    ASSIGN_OR_RETURN(ParallaxTheme * theme, resources.parallax_themes.GetTheme(zone.theme_id));
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
        if (assets.rendering.parallax_textures.contains(element.texture_id)) continue;
        ASSIGN_OR_RETURN(const TextureHandle texture,
                         RequireTextureHandle(resources.textures, element.texture_id,
                                              absl::StrCat("parallax element ", element.id)));
        assets.rendering.parallax_textures.emplace(element.texture_id, texture);
      }
    }
    assets.content.parallax_themes.emplace(theme->id, *theme);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<LoadedLevelAssets> ResolveLevelAssets(const LevelAssetLoaderOptions& resources,
                                                     std::string_view level_id) {
  if (level_id.empty()) return absl::InvalidArgumentError("Level asset ID is empty");

  const std::string requested_level_id(level_id);
  ASSIGN_OR_RETURN(Level * level, resources.levels.GetLevel(requested_level_id));
  if (level == nullptr) return absl::FailedPreconditionError("Level asset resolved to null");
  if (level->id != requested_level_id) {
    return absl::FailedPreconditionError("Level asset lookup resolved the wrong definition");
  }
  if (level->tileset_id.empty()) {
    return absl::FailedPreconditionError("Level asset has no tileset");
  }

  ASSIGN_OR_RETURN(Tileset * tileset, resources.tilesets.GetTileset(level->tileset_id));
  if (tileset == nullptr) return absl::FailedPreconditionError("Level tileset resolved to null");
  if (tileset->id != level->tileset_id) {
    return absl::FailedPreconditionError("Level tileset lookup resolved the wrong definition");
  }
  ASSIGN_OR_RETURN(const TextureHandle tileset_atlas,
                   RequireTextureHandle(resources.textures, tileset->texture_id, "level tileset"));

  LoadedLevelAssets assets{
      .content = {.level = *level, .tileset = *tileset},
      .rendering = {.tileset_atlas = tileset_atlas},
  };
  RETURN_IF_ERROR(LoadEntitySprites(resources, assets));
  RETURN_IF_ERROR(LoadEntityColliders(resources, assets));
  RETURN_IF_ERROR(LoadEntityBlueprints(resources, assets));
  RETURN_IF_ERROR(LoadParallaxThemes(resources, assets));
  return assets;
}

}  // namespace zebes
