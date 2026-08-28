#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
ABSL_FLAG(std::string, operation, "create", "Candidate operation: create or redraw");
ABSL_FLAG(std::string, kind, "", "New asset kind: prop or parallax-artwork");
ABSL_FLAG(std::string, recipe_id, "", "Existing recipe whose domain settings are the template");
ABSL_FLAG(std::string, recipe_name, "",
          "Unique existing recipe name; use instead of copying a recipe ID");
ABSL_FLAG(std::string, name, "", "Name for the new generated asset");
ABSL_FLAG(std::string, prompt, "", "Provider subject prompt");
ABSL_FLAG(std::string, provider, "fake", "Image provider: openai, codex, or fake");
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

absl::StatusOr<std::unique_ptr<ImageGenerationService>> CreateService(const std::string& provider) {
  if (provider == "fake") {
    return ImageGenerationService::Create(CreateFakeImageGenerationClient());
  }
  if (provider == "openai") return ImageGenerationService::CreateOpenAi(OpenAiImageConfig{});
  if (provider == "codex") return ImageGenerationService::CreateCodex(CodexImageConfig{});
  return absl::InvalidArgumentError("--provider must be openai, codex, or fake");
}

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

template <typename Recipe>
absl::StatusOr<std::string> ResolveUniqueRecipeName(const std::vector<Recipe>& recipes,
                                                    const std::string& name) {
  std::string match;
  for (const Recipe& recipe : recipes) {
    if (recipe.name != name) continue;
    if (!match.empty()) {
      return absl::FailedPreconditionError(absl::StrCat("recipe name is not unique: ", name));
    }
    match = recipe.id;
  }
  if (match.empty()) return absl::NotFoundError(absl::StrCat("recipe not found: ", name));
  return match;
}

absl::StatusOr<std::string> ResolveRecipe(Api& api, const std::string& kind) {
  const std::string id = absl::GetFlag(FLAGS_recipe_id);
  const std::string name = absl::GetFlag(FLAGS_recipe_name);
  if (id.empty() == name.empty()) {
    return absl::InvalidArgumentError("set exactly one of --recipe_id or --recipe_name");
  }
  if (!id.empty()) return id;
  if (kind == "prop") return ResolveUniqueRecipeName(api.GetAllPropRecipes(), name);
  if (kind == "parallax-artwork") {
    return ResolveUniqueRecipeName(api.GetAllParallaxArtworkRecipes(), name);
  }
  return absl::InvalidArgumentError("--kind must be prop or parallax-artwork");
}

absl::Status Run() {
  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  if (asset_root.empty()) return absl::InvalidArgumentError("--asset_root must be non-empty");
  const std::string operation = absl::GetFlag(FLAGS_operation);
  const std::string kind = absl::GetFlag(FLAGS_kind);
  const int prop_canvas_tiles_wide = absl::GetFlag(FLAGS_prop_canvas_tiles_wide);
  const int prop_canvas_tiles_high = absl::GetFlag(FLAGS_prop_canvas_tiles_high);
  ASSIGN_OR_RETURN(const std::optional<PropAttachmentMode> prop_attachment_mode,
                   ParsePropAttachment(absl::GetFlag(FLAGS_prop_attachment)));
  ASSIGN_OR_RETURN(const std::optional<PropFreeAnchor> prop_free_anchor,
                   ParsePropFreeAnchor(absl::GetFlag(FLAGS_prop_free_anchor_x),
                                       absl::GetFlag(FLAGS_prop_free_anchor_y)));
  if (operation != "create" && operation != "redraw") {
    return absl::InvalidArgumentError("--operation must be create or redraw");
  }
  if (operation == "redraw" && kind != "parallax-artwork") {
    return absl::InvalidArgumentError("redraw currently supports only --kind=parallax-artwork");
  }
  if (operation == "redraw" && (prop_canvas_tiles_wide != 0 || prop_canvas_tiles_high != 0 ||
                                prop_attachment_mode.has_value() || prop_free_anchor.has_value())) {
    return absl::InvalidArgumentError("prop composition overrides do not apply to redraw");
  }
  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(std::unique_ptr<AssetWorkspace> assets,
                   AssetWorkspace::Create({
                       .config = &config,
                       .texture_resources = &texture_resources,
                       .asset_root = asset_root,
                   }));
  ASSIGN_OR_RETURN(const std::string recipe_id, ResolveRecipe(assets->api(), kind));
  ASSIGN_OR_RETURN(std::unique_ptr<ImageGenerationService> service,
                   CreateService(absl::GetFlag(FLAGS_provider)));
  HeadlessAssetGenerationResult result;
  if (operation == "redraw") {
    ASSIGN_OR_RETURN(
        result, GenerateAssetRedrawCandidateBundle(assets->api(), *service,
                                                   {
                                                       .asset_id = recipe_id,
                                                       .prompt = absl::GetFlag(FLAGS_prompt),
                                                       .output_path = absl::GetFlag(FLAGS_output),
                                                   }));
  } else {
    const HeadlessAssetGenerationRequest request{
        .kind = kind,
        .template_recipe_id = recipe_id,
        .name = absl::GetFlag(FLAGS_name),
        .prompt = absl::GetFlag(FLAGS_prompt),
        .output_path = absl::GetFlag(FLAGS_output),
        .prop_canvas_tiles_wide =
            prop_canvas_tiles_wide == 0 ? std::nullopt : std::optional(prop_canvas_tiles_wide),
        .prop_canvas_tiles_high =
            prop_canvas_tiles_high == 0 ? std::nullopt : std::optional(prop_canvas_tiles_high),
        .prop_attachment_mode = prop_attachment_mode,
        .prop_free_anchor = prop_free_anchor,
    };
    RETURN_IF_ERROR(ValidateHeadlessAssetGenerationRequest(request));
    ASSIGN_OR_RETURN(result, GenerateAssetCandidateBundle(assets->api(), *service, request));
  }
  LOG(INFO) << "Published generated " << operation << " " << kind << " candidate "
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
