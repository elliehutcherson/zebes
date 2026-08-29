#pragma once

#include <map>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "engine/texture_handle.h"
#include "objects/level.h"
#include "objects/parallax_theme.h"
#include "objects/sprite.h"
#include "objects/tileset.h"

namespace zebes {

class Api;

// Frozen runtime definitions and handles for one loaded level. Handles remain
// valid only while the resource store behind the supplying Api is alive.
struct GameLevelAssets {
  Level level;
  Tileset tileset;
  TextureHandle tileset_texture;
  std::map<std::string, Sprite> sprites;
  std::map<std::string, TextureHandle> sprite_textures;
  std::map<std::string, ParallaxTheme> parallax_themes;
  std::map<std::string, TextureHandle> parallax_textures;
};

// Resolves and copies the complete M1 render graph during boot. Missing
// runtime definitions or GPU handles fail before the frame loop begins.
absl::StatusOr<GameLevelAssets> LoadGameLevelAssets(Api& api, std::string_view level_id);

}  // namespace zebes
