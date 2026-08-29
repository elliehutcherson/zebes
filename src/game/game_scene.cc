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
#include "game/game_level_assets.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"

namespace zebes {
namespace {

absl::StatusOr<SpriteLookup> BuildSpriteLookup(const GameLevelAssets& assets) {
  SpriteLookup lookup;
  for (const auto& [sprite_id, sprite] : assets.sprites) {
    auto texture = assets.sprite_textures.find(sprite_id);
    if (texture == assets.sprite_textures.end() || !texture->second) {
      return absl::FailedPreconditionError(
          absl::StrCat("loaded sprite has no texture handle: ", sprite_id));
    }
    lookup.emplace(sprite_id, ResolvedSprite{.sprite = &sprite, .texture = texture->second});
  }
  return lookup;
}

absl::Status AppendParallaxBatch(const GameLevelAssets& assets, const Camera& camera,
                                 const std::string& theme_id, double opacity,
                                 GameSceneFrame& frame) {
  auto theme = assets.parallax_themes.find(theme_id);
  if (theme == assets.parallax_themes.end()) {
    return absl::FailedPreconditionError(
        absl::StrCat("resolved runtime parallax theme is unavailable: ", theme_id));
  }
  ASSIGN_OR_RETURN(SceneParallaxRenderBatch batch,
                   ComposeSceneParallaxRenderBatch(theme->second, camera, assets.parallax_textures,
                                                   {.opacity = opacity}));
  frame.parallax.push_back(std::move(batch));
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<GameSceneFrame> ComposeGameSceneFrame(const GameLevelAssets& assets,
                                                     const Camera& camera) {
  RETURN_IF_ERROR(ValidateSceneCamera(camera));
  ASSIGN_OR_RETURN(const SpriteLookup sprites, BuildSpriteLookup(assets));

  GameSceneFrame frame{.camera = camera};
  ASSIGN_OR_RETURN(frame.environment,
                   ResolveParallaxEnvironment(assets.level.zones, camera.position));
  if (frame.environment.has_value()) {
    RETURN_IF_ERROR(
        AppendParallaxBatch(assets, camera, frame.environment->primary.theme_id, 1.0, frame));
    if (frame.environment->secondary.has_value() && frame.environment->secondary_weight > 0.0) {
      RETURN_IF_ERROR(AppendParallaxBatch(assets, camera, frame.environment->secondary->theme_id,
                                          frame.environment->secondary_weight, frame));
    }
  }

  frame.world_layers.reserve(assets.level.layers.size());
  for (const WorldLayer& layer : assets.level.layers) {
    ASSIGN_OR_RETURN(SceneTileRenderBatch tiles, ComposeSceneLevelTileRenderBatch({
                                                     .level = assets.level,
                                                     .layer = layer,
                                                     .tileset = assets.tileset,
                                                     .atlas_texture = assets.tileset_texture,
                                                     .camera = camera,
                                                 }));
    ASSIGN_OR_RETURN(std::vector<SceneEntityRenderItem> entities,
                     ComposeSceneEntityRenderItems(layer.entities, sprites));
    frame.world_layers.push_back({
        .layer_id = layer.id,
        .tiles = std::move(tiles),
        .entities = std::move(entities),
    });
  }
  return frame;
}

}  // namespace zebes
