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
#include "absl/strings/str_join.h"
#include "api/asset_workspace.h"
#include "common/config.h"
#include "common/status_macros.h"
#include "curation/candidate_commit.h"
#include "curation/parallax_artwork_reviewer.h"
#include "curation/parallax_theme_reviewer.h"
#include "curation/prop_reviewer.h"
#include "curation/registry.h"
#include "curation/review.h"
#include "curation/sprite_reviewer.h"
#include "curation/terrain_reviewer.h"
#include "curation/tileset_reviewer.h"
#include "nlohmann/json.hpp"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, kind, "", "Asset kind to review: parallax-theme or prop");
ABSL_FLAG(std::string, id, "", "Stable ID of the asset to review");
ABSL_FLAG(std::string, output, "", "New directory in which to publish the review bundle");
ABSL_FLAG(std::string, candidate, "", "Optional kind-owned JSON candidate to review");
ABSL_FLAG(bool, commit, false, "Commit a reviewed candidate through the production API");
ABSL_FLAG(bool, list_kinds, false, "List registered review kinds and exit");

namespace zebes {
namespace {

absl::Status RegisterReviewers(CurationRegistry& registry) {
  RETURN_IF_ERROR(registry.Add(std::make_unique<ParallaxArtworkReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<ParallaxThemeReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<PropReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<SpriteReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<TerrainReviewer>()));
  return registry.Add(std::make_unique<TilesetReviewer>());
}

absl::Status Run() {
  CurationRegistry registry;
  RETURN_IF_ERROR(RegisterReviewers(registry));
  if (absl::GetFlag(FLAGS_list_kinds)) {
    std::cout << absl::StrJoin(registry.Kinds(), "\n") << '\n';
    return absl::OkStatus();
  }

  const std::string asset_root = absl::GetFlag(FLAGS_asset_root);
  const std::string kind = absl::GetFlag(FLAGS_kind);
  const std::string asset_id = absl::GetFlag(FLAGS_id);
  const std::string output = absl::GetFlag(FLAGS_output);
  if (asset_root.empty() || kind.empty() || asset_id.empty() || output.empty()) {
    return absl::InvalidArgumentError(
        "--asset_root, --kind, --id, and --output must all be non-empty");
  }
  const std::string candidate_path = absl::GetFlag(FLAGS_candidate);
  const bool commit = absl::GetFlag(FLAGS_commit);
  if (commit && candidate_path.empty()) {
    return absl::InvalidArgumentError("--commit requires --candidate");
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
  CurationReviewRequest request{.asset_id = asset_id};
  CurationReview review;
  nlohmann::json candidate;
  if (candidate_path.empty()) {
    ASSIGN_OR_RETURN(review, registry.Review(assets->api(), kind, request));
  } else {
    std::filesystem::path candidate_file(candidate_path);
    request.candidate_root = candidate_file.has_parent_path()
                                 ? candidate_file.parent_path().string()
                                 : std::filesystem::current_path().string();
    std::ifstream stream(candidate_path);
    if (!stream.is_open()) {
      return absl::NotFoundError(absl::StrCat("could not open candidate: ", candidate_path));
    }
    try {
      stream >> candidate;
    } catch (const nlohmann::json::exception& error) {
      return absl::InvalidArgumentError(absl::StrCat("candidate JSON is invalid: ", error.what()));
    }
    if (commit) {
      RETURN_IF_ERROR(
          CommitCandidateWithEvidence(registry, assets->api(), kind, request, candidate, output));
      const std::string committed_output = CommittedCurationOutputPath(output);
      LOG(INFO) << "Committed reviewed " << kind << " candidate " << asset_id;
      LOG(INFO) << "Published post-commit review at " << committed_output;
      std::cout << committed_output << "/manifest.json\n";
      return absl::OkStatus();
    }
    ASSIGN_OR_RETURN(review, registry.ReviewCandidate(assets->api(), kind, request, candidate));
  }
  RETURN_IF_ERROR(PublishCurationReview(review, output));
  LOG(INFO) << "Published " << review.artifacts.size() << " review artifacts for " << kind << " "
            << asset_id << " at " << output;
  std::cout << output << "/manifest.json\n";
  return absl::OkStatus();
}

}  // namespace
}  // namespace zebes

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  const absl::Status status = zebes::Run();
  if (!status.ok()) {
    LOG(ERROR) << "Curation review failed: " << status;
    return 1;
  }
  return 0;
}
