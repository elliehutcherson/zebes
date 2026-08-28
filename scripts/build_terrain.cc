#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/asset_workspace.h"
#include "authoring/terrain_builder.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, spec, "", "Versioned terrain build specification");

namespace zebes {
namespace {

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string spec_path = absl::GetFlag(FLAGS_spec);
  if (asset_root.empty() || spec_path.empty()) {
    return absl::InvalidArgumentError("--asset_root and --spec must be non-empty");
  }
  ASSIGN_OR_RETURN(const TerrainBuildSpec spec, ReadTerrainBuildSpec(spec_path));
  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> assets,
                   AssetWorkspace::Create({
                       .config = &config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                       .access = AssetWorkspace::Access::kReadWrite,
                   }));
  ASSIGN_OR_RETURN(const TerrainBuildResult result, BuildTerrain(assets->api(), spec));
  std::cout << "operation=" << (result.created ? "created" : "regenerated") << '\n'
            << "recipe_id=" << result.recipe_id << '\n'
            << "tileset_id=" << result.tileset_id << '\n'
            << "texture_id=" << result.texture_id << '\n'
            << "tile_count=" << result.tile_count << '\n';
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Could not build terrain: " << status;
    return 1;
  }
  return 0;
}
