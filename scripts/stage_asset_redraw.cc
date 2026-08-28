#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/api.h"
#include "api/asset_workspace.h"
#include "common/atomic_directory_publisher.h"
#include "common/config.h"
#include "common/image_digest.h"
#include "common/image_io.h"
#include "common/status_macros.h"
#include "common/utc_timestamp.h"
#include "generation/generated_asset_candidate.h"
#include "nlohmann/json.hpp"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, kind, "parallax-artwork", "Existing generated asset kind");
ABSL_FLAG(std::string, id, "", "Existing recipe ID whose retained source will be redrawn");
ABSL_FLAG(std::string, input, "", "Replacement source PNG to stage");
ABSL_FLAG(std::string, provider, "", "Provider that produced the replacement image");
ABSL_FLAG(std::string, model, "", "Model that produced the replacement image");
ABSL_FLAG(std::string, prompt, "", "Exact prompt submitted for the replacement image");
ABSL_FLAG(std::string, output, "", "New directory in which to publish the redraw candidate");

namespace zebes {
namespace {

constexpr char kSourceFilename[] = "redrawn-source.png";
constexpr char kCandidateFilename[] = "candidate.json";
constexpr char kManifestFilename[] = "manifest.json";

absl::Status WriteJson(const std::filesystem::path& path, const nlohmann::json& json) {
  std::ofstream stream(path);
  if (!stream.is_open()) {
    return absl::InternalError(absl::StrCat("could not open JSON output: ", path.string()));
  }
  stream << json.dump(2) << '\n';
  if (!stream.good()) {
    return absl::InternalError(absl::StrCat("could not write JSON output: ", path.string()));
  }
  return absl::OkStatus();
}

absl::Status Run() {
  const std::string kind = absl::GetFlag(FLAGS_kind);
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string asset_id = absl::GetFlag(FLAGS_id);
  const std::string input = absl::GetFlag(FLAGS_input);
  const std::string provider = absl::GetFlag(FLAGS_provider);
  const std::string model = absl::GetFlag(FLAGS_model);
  const std::string prompt = absl::GetFlag(FLAGS_prompt);
  const std::string output = absl::GetFlag(FLAGS_output);
  if (kind != "parallax-artwork") {
    return absl::InvalidArgumentError("--kind currently supports only 'parallax-artwork'");
  }
  if (asset_root.empty() || asset_id.empty() || input.empty() || provider.empty() ||
      model.empty() || prompt.empty() || output.empty()) {
    return absl::InvalidArgumentError(
        "--asset_root, --id, --input, --provider, --model, --prompt, and --output must be "
        "non-empty");
  }
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(output));
  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> assets,
                   AssetWorkspace::Create({
                       .config = &config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                   }));
  ASSIGN_OR_RETURN(ParallaxArtworkRecipe * recipe,
                   assets->api().GetParallaxArtworkRecipe(asset_id));
  if (recipe == nullptr) {
    return absl::FailedPreconditionError("parallax artwork recipe lookup returned null");
  }
  ASSIGN_OR_RETURN(SourceArtwork * source,
                   assets->api().GetSourceArtwork(recipe->source_artwork_id));
  if (source == nullptr) {
    return absl::FailedPreconditionError("parallax artwork source lookup returned null");
  }
  ASSIGN_OR_RETURN(const RgbaImage current_source,
                   assets->api().ReadSourceArtworkPixels(source->id));
  ASSIGN_OR_RETURN(const std::string current_source_digest, RgbaImageDigest(current_source));
  if (current_source_digest != source->content_digest) {
    return absl::FailedPreconditionError("current source pixels do not match their catalog digest");
  }
  ASSIGN_OR_RETURN(const RgbaImage current_texture,
                   assets->api().ReadTexturePixels(recipe->texture_id));
  ASSIGN_OR_RETURN(const std::string current_texture_digest, RgbaImageDigest(current_texture));
  if (current_texture_digest != recipe->final_pixel_digest) {
    return absl::FailedPreconditionError(
        "current parallax pixels do not match their recipe digest");
  }
  ASSIGN_OR_RETURN(const RgbaImage pixels, ReadPng(input));
  ASSIGN_OR_RETURN(const std::string digest, RgbaImageDigest(pixels));

  const GeneratedParallaxArtworkRedrawCandidate candidate{
      .asset_id = asset_id,
      .expected_source_digest = current_source_digest,
      .expected_final_pixel_digest = current_texture_digest,
      .source =
          {
              .relative_path = kSourceFilename,
              .width = pixels.width,
              .height = pixels.height,
              .content_digest = digest,
              .provenance =
                  {
                      .provider = provider,
                      .model = model,
                      .submitted_prompt = prompt,
                      .generated_at_utc = CurrentUtcTimestamp(),
                  },
          },
  };
  const nlohmann::json candidate_json = GeneratedParallaxArtworkRedrawCandidateToJson(candidate);
  RETURN_IF_ERROR(GeneratedParallaxArtworkRedrawCandidateFromJson(candidate_json).status());
  const nlohmann::json manifest{
      {"schema_version", 1},
      {"bundle", "generated-asset-redraw-candidate"},
      {"kind", kind},
      {"asset_id", asset_id},
      {"candidate", kCandidateFilename},
      {"artifacts",
       {{{"id", "redrawn-source"},
         {"path", kSourceFilename},
         {"rgba_sha256", digest},
         {"width", pixels.width},
         {"height", pixels.height}}}},
  };
  RETURN_IF_ERROR(PublishNewDirectoryAtomically(
      output, [&](const std::filesystem::path& staging) -> absl::Status {
        RETURN_IF_ERROR(WritePng((staging / kSourceFilename).string(), pixels.width, pixels.height,
                                 pixels.pixels));
        RETURN_IF_ERROR(WriteJson(staging / kCandidateFilename, candidate_json));
        return WriteJson(staging / kManifestFilename, manifest);
      }));
  std::cout << (std::filesystem::path(output) / kCandidateFilename).string() << '\n';
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Asset redraw staging failed: " << status;
    return 1;
  }
  return 0;
}
