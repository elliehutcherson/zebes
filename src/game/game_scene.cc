#include "game/game_scene.h"

#include <map>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/status_macros.h"
#include "engine/scene_composition.h"
#include "engine/scene_types.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "resources/loaded_level_assets.h"

namespace zebes {
namespace {

absl::StatusOr<SpriteLookup> BuildSpriteLookup(const LoadedLevelAssets& assets) {
  SpriteLookup lookup;
  for (const auto& [sprite_id, sprite] : assets.content.sprites) {
    auto texture = assets.rendering.sprite_textures.find(sprite_id);
    if (texture == assets.rendering.sprite_textures.end() || !texture->second) {
      return absl::FailedPreconditionError(
          absl::StrCat("loaded sprite has no texture handle: ", sprite_id));
    }
    lookup.emplace(sprite_id, ResolvedSprite{.sprite = &sprite, .texture = texture->second});
  }
  return lookup;
}

absl::StatusOr<SceneParallaxRenderBatch> ComposeParallaxBatch(const LoadedLevelAssets& assets,
                                                              const Camera& camera,
                                                              const std::string& theme_id,
                                                              double opacity) {
  auto theme = assets.content.parallax_themes.find(theme_id);
  if (theme == assets.content.parallax_themes.end()) {
    return absl::FailedPreconditionError(
        absl::StrCat("resolved runtime parallax theme is unavailable: ", theme_id));
  }
  return ComposeSceneParallaxRenderBatch(theme->second, camera, assets.rendering.parallax_textures,
                                         {.opacity = opacity});
}

}  // namespace

absl::StatusOr<GameSceneFrame> ComposeGameSceneFrame(const LoadedLevelAssets& assets,
                                                     const Camera& camera,
                                                     const GameSceneCompositionOptions& options) {
  RETURN_IF_ERROR(ValidateSceneCamera(camera));
  ASSIGN_OR_RETURN(const SpriteLookup sprites, BuildSpriteLookup(assets));

  GameSceneFrame frame{.camera = camera};
  ASSIGN_OR_RETURN(frame.environment,
                   ResolveParallaxEnvironment(assets.content.level.zones, camera.position));
  if (frame.environment.has_value()) {
    ASSIGN_OR_RETURN(
        SceneParallaxRenderBatch primary,
        ComposeParallaxBatch(assets, camera, frame.environment->primary.theme_id, 1.0));
    frame.parallax.push_back(std::move(primary));
    if (frame.environment->secondary.has_value() && frame.environment->secondary_weight > 0.0) {
      ASSIGN_OR_RETURN(SceneParallaxRenderBatch secondary,
                       ComposeParallaxBatch(assets, camera, frame.environment->secondary->theme_id,
                                            frame.environment->secondary_weight));
      frame.parallax.push_back(std::move(secondary));
    }
  }

  frame.world_layers.reserve(assets.content.level.layers.size());
  for (const WorldLayer& layer : assets.content.level.layers) {
    ASSIGN_OR_RETURN(SceneTileRenderBatch tiles,
                     ComposeSceneLevelTileRenderBatch({
                         .level = assets.content.level,
                         .layer = layer,
                         .tileset = assets.content.tileset,
                         .atlas_texture = assets.rendering.tileset_atlas,
                         .camera = camera,
                     }));
    ASSIGN_OR_RETURN(
        std::vector<SceneEntityRenderItem> entities,
        ComposeSceneEntityRenderItems(layer.entities, sprites,
                                      {.transform_overrides = options.transform_overrides}));
    frame.world_layers.push_back({
        .layer_id = layer.id,
        .tiles = std::move(tiles),
        .entities = std::move(entities),
    });
  }
  return frame;
}

}  // namespace zebes
