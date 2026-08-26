#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "generation/codex_image_client.h"
#include "generation/fake_image_generation_client.h"
#include "generation/headless_asset_generation.h"
#include "generation/image_generation_service.h"
#include "generation/openai_image_client.h"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, kind, "", "New asset kind: prop or parallax-artwork");
ABSL_FLAG(std::string, recipe_id, "", "Existing recipe whose domain settings are the template");
ABSL_FLAG(std::string, name, "", "Name for the new generated asset");
ABSL_FLAG(std::string, prompt, "", "Provider subject prompt");
ABSL_FLAG(std::string, provider, "fake", "Image provider: openai, codex, or fake");
ABSL_FLAG(std::string, output, "", "New directory in which to publish the candidate bundle");

namespace zebes {
namespace {

absl::StatusOr<std::unique_ptr<ImageGenerationService>> CreateService(const std::string& provider) {
  if (provider == "fake") {
    return ImageGenerationService::Create(CreateFakeImageGenerationClient());
  }
  if (provider == "openai") return ImageGenerationService::CreateOpenAi(OpenAiImageConfig{});
  if (provider == "codex") return ImageGenerationService::CreateCodex(CodexImageConfig{});
  return absl::InvalidArgumentError("--provider must be openai, codex, or fake");
}

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  if (asset_root.empty()) return absl::InvalidArgumentError("--asset_root must be non-empty");
  const HeadlessAssetGenerationRequest request{
      .kind = absl::GetFlag(FLAGS_kind),
      .template_recipe_id = absl::GetFlag(FLAGS_recipe_id),
      .name = absl::GetFlag(FLAGS_name),
      .prompt = absl::GetFlag(FLAGS_prompt),
      .output_path = absl::GetFlag(FLAGS_output),
  };
  RETURN_IF_ERROR(ValidateHeadlessAssetGenerationRequest(request));
  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> assets,
                   AssetWorkspace::Create({
                       .config = &config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                   }));
  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationService> service,
                   CreateService(absl::GetFlag(FLAGS_provider)));
  ASSIGN_OR_RETURN(HeadlessAssetGenerationResult result,
                   GenerateAssetCandidateBundle(assets->api(), *service, request));
  LOG(INFO) << "Published generated " << absl::GetFlag(FLAGS_kind) << " candidate "
            << result.asset_id << " at " << absl::GetFlag(FLAGS_output);
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
    LOG(ERROR) << "Asset generation failed: " << status;
    return 1;
  }
  return 0;
}
