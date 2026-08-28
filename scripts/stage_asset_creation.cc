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
#include "common/image_io.h"
#include "common/status_macros.h"
#include "generation/headless_asset_generation.h"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, kind, "", "New asset kind: prop or parallax-artwork");
ABSL_FLAG(std::string, recipe_id, "", "Existing recipe whose domain settings are the template");
ABSL_FLAG(std::string, name, "", "Name for the new generated asset");
ABSL_FLAG(std::string, input, "", "Already-generated source PNG to stage");
ABSL_FLAG(std::string, provider, "", "Provider that produced the source image");
ABSL_FLAG(std::string, model, "", "Model that produced the source image");
ABSL_FLAG(std::string, prompt, "", "Exact prompt submitted for the source image");
ABSL_FLAG(std::string, output, "", "New directory in which to publish the candidate bundle");

namespace zebes {
namespace {

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string input = absl::GetFlag(FLAGS_input);
  if (asset_root.empty() || input.empty()) {
    return absl::InvalidArgumentError("--asset_root and --input must be non-empty");
  }
  const HeadlessAssetStagingRequest request{
      .kind = absl::GetFlag(FLAGS_kind),
      .template_recipe_id = absl::GetFlag(FLAGS_recipe_id),
      .name = absl::GetFlag(FLAGS_name),
      .prompt = absl::GetFlag(FLAGS_prompt),
      .provider = absl::GetFlag(FLAGS_provider),
      .model = absl::GetFlag(FLAGS_model),
      .output_path = absl::GetFlag(FLAGS_output),
  };
  RETURN_IF_ERROR(ValidateHeadlessAssetStagingRequest(request));
  ASSIGN_OR_RETURN(const RgbaImage image, ReadPng(input));
  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> assets,
                   AssetWorkspace::Create({
                       .config = &config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                   }));
  ASSIGN_OR_RETURN(const HeadlessAssetGenerationResult result,
                   StageAssetCandidateBundle(assets->api(), image, request));
  LOG(INFO) << "Published staged " << request.kind << " candidate " << result.asset_id << " at "
            << request.output_path;
  std::cout << result.manifest_path << '\n';
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Asset creation staging failed: " << status;
    return 1;
  }
  return 0;
}
