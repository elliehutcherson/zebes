#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

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
#include "generation/image_generation_service.h"
#include "generation/openai_image_client.h"
#include "platform/headless/headless_texture_store.h"
#include "scripts/pose_conditioned_animation_batch.h"

ABSL_FLAG(std::string, asset_root, "assets",
          "Root containing config.json and managed SourceArtwork catalogs");
ABSL_FLAG(std::string, manifest, "", "Locked schema-v1 pose-conditioned experiment manifest");
ABSL_FLAG(std::string, phase, "", "Experiment phase: pilot or batch");
ABSL_FLAG(std::string, output, "", "New directory for immutable experiment evidence");
ABSL_FLAG(std::string, pilot_approval, "",
          "Reviewed schema-v1 pilot approval; required only for batch");

namespace zebes {
namespace {

absl::StatusOr<std::unique_ptr<ImageGenerationService>> CreateService(
    const PoseConditionedAnimationProviderConfig& config) {
  if (config.provider == "fake") {
    if (config.model != "zebes-fake-v1") {
      return absl::InvalidArgumentError(
          "the fake pose-batch provider requires model zebes-fake-v1");
    }
    return ImageGenerationService::Create(CreateFakeImageGenerationClient());
  }
  if (config.provider == "openai") {
    OpenAiImageConfig provider;
    provider.model = config.model;
    provider.size = absl::StrCat(config.expected_output_width, "x", config.expected_output_height);
    return ImageGenerationService::CreateOpenAi(std::move(provider));
  }
  if (config.provider == "codex") {
    CodexImageConfig provider;
    provider.model = config.model;
    return ImageGenerationService::CreateCodex(std::move(provider));
  }
  return absl::InvalidArgumentError("pose batch provider must be fake, openai, or codex");
}

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string manifest = absl::GetFlag(FLAGS_manifest);
  const std::string phase = absl::GetFlag(FLAGS_phase);
  const std::string output = absl::GetFlag(FLAGS_output);
  if (asset_root.empty() || manifest.empty() || phase.empty() || output.empty()) {
    return absl::InvalidArgumentError(
        "--asset_root, --manifest, --phase, and --output must be non-empty");
  }
  ASSIGN_OR_RETURN(const PoseConditionedAnimationPhase parsed_phase,
                   ParsePoseConditionedAnimationPhase(phase));
  ASSIGN_OR_RETURN(const PoseConditionedAnimationProviderConfig provider,
                   LoadPoseConditionedAnimationProviderConfig(manifest));
  ASSIGN_OR_RETURN(EngineConfig engine_config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> assets,
                   AssetWorkspace::Create({
                       .config = &engine_config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                   }));
  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationService> service, CreateService(provider));
  RETURN_IF_ERROR(RunPoseConditionedAnimationBatch(
      assets->api(), *service,
      {
          .manifest_path = manifest,
          .output_path = output,
          .phase = parsed_phase,
          .pilot_approval_path = absl::GetFlag(FLAGS_pilot_approval),
      }));
  std::cout << (std::filesystem::path(output) / "manifest.json").string() << '\n';
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Pose-conditioned animation experiment failed: " << status;
    return 1;
  }
  return 0;
}
