#include <iostream>
#include <memory>
#include <optional>
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
ABSL_FLAG(int, prop_canvas_tiles_wide, 0,
          "Prop output width in tiles; set with --prop_canvas_tiles_high, or zero to inherit");
ABSL_FLAG(int, prop_canvas_tiles_high, 0,
          "Prop output height in tiles; set with --prop_canvas_tiles_wide, or zero to inherit");
ABSL_FLAG(std::string, prop_attachment, "inherit",
          "Prop attachment override: inherit, grounded, ceiling, or free");
ABSL_FLAG(int, prop_free_anchor_x, -1,
          "Free prop anchor X in output pixels; set with --prop_free_anchor_y");
ABSL_FLAG(int, prop_free_anchor_y, -1,
          "Free prop anchor Y in output pixels; set with --prop_free_anchor_x");

namespace zebes {
namespace {

absl::StatusOr<std::optional<PropAttachmentMode>> ParsePropAttachment(const std::string& value) {
  if (value == "inherit") return std::nullopt;
  if (value == "grounded") return PropAttachmentMode::kGrounded;
  if (value == "ceiling") return PropAttachmentMode::kCeiling;
  if (value == "free") return PropAttachmentMode::kFree;
  return absl::InvalidArgumentError(
      "--prop_attachment must be inherit, grounded, ceiling, or free");
}

absl::StatusOr<std::optional<PropFreeAnchor>> ParsePropFreeAnchor(int x, int y) {
  if (x == -1 && y == -1) return std::nullopt;
  if (x < 0 || y < 0) {
    return absl::InvalidArgumentError(
        "--prop_free_anchor_x and --prop_free_anchor_y must be non-negative and provided together");
  }
  return PropFreeAnchor{.x = x, .y = y};
}

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string input = absl::GetFlag(FLAGS_input);
  if (asset_root.empty() || input.empty()) {
    return absl::InvalidArgumentError("--asset_root and --input must be non-empty");
  }
  const int prop_canvas_tiles_wide = absl::GetFlag(FLAGS_prop_canvas_tiles_wide);
  const int prop_canvas_tiles_high = absl::GetFlag(FLAGS_prop_canvas_tiles_high);
  ASSIGN_OR_RETURN(const std::optional<PropAttachmentMode> prop_attachment_mode,
                   ParsePropAttachment(absl::GetFlag(FLAGS_prop_attachment)));
  ASSIGN_OR_RETURN(const std::optional<PropFreeAnchor> prop_free_anchor,
                   ParsePropFreeAnchor(absl::GetFlag(FLAGS_prop_free_anchor_x),
                                       absl::GetFlag(FLAGS_prop_free_anchor_y)));
  const HeadlessAssetStagingRequest request{
      .kind = absl::GetFlag(FLAGS_kind),
      .template_recipe_id = absl::GetFlag(FLAGS_recipe_id),
      .name = absl::GetFlag(FLAGS_name),
      .prompt = absl::GetFlag(FLAGS_prompt),
      .provider = absl::GetFlag(FLAGS_provider),
      .model = absl::GetFlag(FLAGS_model),
      .output_path = absl::GetFlag(FLAGS_output),
      .prop_canvas_tiles_wide =
          prop_canvas_tiles_wide == 0 ? std::nullopt : std::optional(prop_canvas_tiles_wide),
      .prop_canvas_tiles_high =
          prop_canvas_tiles_high == 0 ? std::nullopt : std::optional(prop_canvas_tiles_high),
      .prop_attachment_mode = prop_attachment_mode,
      .prop_free_anchor = prop_free_anchor,
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
