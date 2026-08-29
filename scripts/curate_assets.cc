#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

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
#include "curation/level_reviewer.h"
#include "curation/parallax_artwork_reviewer.h"
#include "curation/parallax_theme_reviewer.h"
#include "curation/prop_reviewer.h"
#include "curation/registry.h"
#include "curation/review.h"
#include "curation/sprite_reviewer.h"
#include "curation/terrain_reviewer.h"
#include "curation/tileset_reviewer.h"
#include "nlohmann/json.hpp"
#include "objects/entity.h"
#include "platform/headless/headless_texture_store.h"

ABSL_FLAG(std::string, asset_root, "assets", "Root containing config.json and asset catalogs");
ABSL_FLAG(std::string, kind, "", "Registered visual asset kind to review");
ABSL_FLAG(std::string, id, "", "Stable ID of the asset to review");
ABSL_FLAG(std::string, output, "", "New directory in which to publish the review bundle");
ABSL_FLAG(std::string, candidate, "",
          "Optional kind-owned JSON candidate; a focused level review substitutes a prop");
ABSL_FLAG(bool, commit, false, "Commit a reviewed candidate through the production API");
ABSL_FLAG(bool, list_kinds, false, "List registered review kinds and exit");
ABSL_FLAG(uint64_t, focus_entity_id, 0,
          "For level reviews, render deterministic cameras focused on this placed entity");

namespace zebes {
namespace {

using SteadyClock = std::chrono::steady_clock;

int64_t ElapsedMilliseconds(SteadyClock::time_point start, SteadyClock::time_point end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

void LogTimings(SteadyClock::time_point command_started, SteadyClock::time_point workspace_loaded,
                SteadyClock::time_point completed) {
  std::cerr << "curation timing: workspace_load_ms="
            << ElapsedMilliseconds(command_started, workspace_loaded)
            << " review_and_publish_ms=" << ElapsedMilliseconds(workspace_loaded, completed)
            << " total_ms=" << ElapsedMilliseconds(command_started, completed) << '\n';
}

struct CandidateInput {
  std::filesystem::path root;
  nlohmann::json document;
};

absl::StatusOr<std::optional<CandidateInput>> ReadCandidateInput(
    const std::string& candidate_path) {
  if (candidate_path.empty()) return std::nullopt;
  const std::filesystem::path candidate_file(candidate_path);
  std::ifstream stream(candidate_file);
  if (!stream.is_open()) {
    return absl::NotFoundError(absl::StrCat("could not open candidate: ", candidate_path));
  }
  nlohmann::json document;
  try {
    stream >> document;
  } catch (const nlohmann::json::exception& error) {
    return absl::InvalidArgumentError(absl::StrCat("candidate JSON is invalid: ", error.what()));
  }
  return CandidateInput{
      .root = candidate_file.has_parent_path() ? candidate_file.parent_path()
                                               : std::filesystem::current_path(),
      .document = std::move(document),
  };
}

absl::Status RegisterReviewers(CurationRegistry& registry) {
  RETURN_IF_ERROR(registry.Add(std::make_unique<LevelReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<ParallaxArtworkReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<ParallaxThemeReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<PropReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<SpriteReviewer>()));
  RETURN_IF_ERROR(registry.Add(std::make_unique<TerrainReviewer>()));
  return registry.Add(std::make_unique<TilesetReviewer>());
}

absl::Status Run() {
  const SteadyClock::time_point command_started = SteadyClock::now();
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
  const uint64_t focus_entity_id = absl::GetFlag(FLAGS_focus_entity_id);
  if (commit && candidate_path.empty()) {
    return absl::InvalidArgumentError("--commit requires --candidate");
  }
  if (focus_entity_id != Entity::kInvalidId && kind != "level") {
    return absl::InvalidArgumentError("--focus_entity_id is supported only for --kind=level");
  }
  if (kind == "level" && !candidate_path.empty() && focus_entity_id == Entity::kInvalidId) {
    return absl::InvalidArgumentError(
        "a level --candidate requires --focus_entity_id as its transient replacement target");
  }
  if (kind == "level" && !candidate_path.empty() && commit) {
    return absl::InvalidArgumentError("a transient level candidate cannot be committed");
  }
  ASSIGN_OR_RETURN(std::optional<CandidateInput> candidate, ReadCandidateInput(candidate_path));

  ASSIGN_OR_RETURN(EngineConfig config,
                   EngineConfig::Load(absl::StrCat(asset_root, "/config.json")));
  HeadlessTextureStore texture_resources;
  ASSIGN_OR_RETURN(
      std::unique_ptr<AssetWorkspace> assets,
      AssetWorkspace::Create({
          .config = &config,
          .texture_resources = &texture_resources,
          .asset_root = asset_root,
          .access = commit ? AssetWorkspace::Access::kReadWrite : AssetWorkspace::Access::kReadOnly,
      }));
  const SteadyClock::time_point workspace_loaded = SteadyClock::now();
  CurationReviewRequest request{.asset_id = asset_id};
  if (focus_entity_id != Entity::kInvalidId) request.focus_entity_id = focus_entity_id;
  if (!candidate.has_value()) {
    ASSIGN_OR_RETURN(const size_t artifact_count,
                     registry.PublishReview(assets->api(), kind, request, output));
    LOG(INFO) << "Published " << artifact_count << " review artifacts for " << kind << " "
              << asset_id << " at " << output;
    LogTimings(command_started, workspace_loaded, SteadyClock::now());
    std::cout << output << "/manifest.json\n";
    return absl::OkStatus();
  }

  request.candidate_root = candidate->root.string();
  if (commit) {
    RETURN_IF_ERROR(CommitCandidateWithEvidence(registry, assets->api(), kind, request,
                                                candidate->document, output));
    const std::string committed_output = CommittedCurationOutputPath(output);
    LOG(INFO) << "Committed reviewed " << kind << " candidate " << asset_id;
    LOG(INFO) << "Published post-commit review at " << committed_output;
    LogTimings(command_started, workspace_loaded, SteadyClock::now());
    std::cout << committed_output << "/manifest.json\n";
    return absl::OkStatus();
  }
  ASSIGN_OR_RETURN(
      const size_t artifact_count,
      registry.PublishCandidateReview(assets->api(), kind, request, candidate->document, output));
  LOG(INFO) << "Published " << artifact_count << " review artifacts for " << kind << " " << asset_id
            << " at " << output;
  LogTimings(command_started, workspace_loaded, SteadyClock::now());
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
