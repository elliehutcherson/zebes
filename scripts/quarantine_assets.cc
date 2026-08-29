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
#include "common/config.h"
#include "common/status_macros.h"
#include "curation/generated_asset_quarantine.h"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, kind, "", "Generated asset kind: terrain, prop, or parallax-artwork");
ABSL_FLAG(std::string, recipe_id, "", "Recipe ID whose complete generated graph to quarantine");
ABSL_FLAG(std::string, output, "", "New recovery directory outside the live asset root");

namespace zebes {
namespace {

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string recipe_id = absl::GetFlag(FLAGS_recipe_id);
  const std::string output = absl::GetFlag(FLAGS_output);
  if (asset_root.empty() || recipe_id.empty() || output.empty()) {
    return absl::InvalidArgumentError(
        "--asset_root, --recipe_id, and --output must all be non-empty");
  }
  ASSIGN_OR_RETURN(const GeneratedAssetKind kind,
                   ParseGeneratedAssetKind(absl::GetFlag(FLAGS_kind)));
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
  RETURN_IF_ERROR(QuarantineGeneratedAsset(assets->api(), {
                                                              .asset_root = asset_root,
                                                              .output_path = output,
                                                              .kind = kind,
                                                              .recipe_id = recipe_id,
                                                          }));
  std::cout << "Quarantined " << GeneratedAssetKindId(kind) << " recipe " << recipe_id << " to "
            << output << '\n';
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << status;
    return 1;
  }
  return 0;
}
