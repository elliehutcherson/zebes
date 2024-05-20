#include "config.h"

#include <iostream>

#include "SDL_video.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#if __APPLE__
#include <mach-o/dyld.h>
#elif __linux__
#include <filesystem>
#endif

namespace zebes {
namespace {

std::string GetExecPath() {
  std::string path;
#if __APPLE__
  char buf[PATH_MAX];
  uint32_t bufsize = PATH_MAX;
  if (!_NSGetExecutablePath(buf, &bufsize)) {
    path = std::string(buf);
  }
#elif __linux__
  path = std::filesystem::canonical("/proc/self/exe");
#endif
  std::vector<std::string> paths = absl::StrSplit(path, "/");
  paths.pop_back();
  return absl::StrJoin(paths, "/");
}

} // namespace

PathConfig::PathConfig(absl::string_view execute_path)
    : execute(execute_path),
      assets(absl::StrFormat("%s/%s", execute, kZebesAssetsPath)),
      tile_matrix(absl::StrFormat("%s/%s", assets, kZebesTileMatrixPath)),
      background(absl::StrFormat("%s/%s", execute, kBackgroundPath)),
      tile_set(absl::StrFormat("%s/%s", execute, kTileSetPath)),
      custom_tile_set(absl::StrFormat("%s/%s", execute, kCustomTileSetPath)),
      hud_font(absl::StrFormat("%s/%s", assets, kHudFont)) {}

GameConfig GameConfig::Create() {
  WindowConfig window_config{
      .title = "Zebes",
      .xpos = SDL_WINDOWPOS_CENTERED,
      .ypos = SDL_WINDOWPOS_CENTERED,
      .width = 1400,
      .height = 640,
  };
  PathConfig path_config(GetExecPath());

  GameConfig config(window_config, path_config);

  return config;
}

GameConfig::GameConfig(WindowConfig window_config, PathConfig path_config)
    : window(window_config), paths(path_config) {}

} // namespace zebes