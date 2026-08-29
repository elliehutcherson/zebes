#include "curation/candidate_commit.h"

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "common/atomic_directory_publisher.h"
#include "common/status_macros.h"

namespace zebes {

std::string CommittedCurationOutputPath(std::string_view reviewed_output_path) {
  std::string path(reviewed_output_path);
  path.append("-committed");
  return path;
}

absl::Status CommitCandidateWithEvidence(CurationRegistry& registry, Api& api,
                                         std::string_view kind,
                                         const CurationReviewRequest& request,
                                         const nlohmann::json& candidate,
                                         const std::string& reviewed_output_path) {
  const std::string committed_output = CommittedCurationOutputPath(reviewed_output_path);
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(reviewed_output_path));
  RETURN_IF_ERROR(ValidateNewDirectoryDestination(committed_output));

  RETURN_IF_ERROR(
      registry.PublishCandidateReview(api, kind, request, candidate, reviewed_output_path)
          .status());
  RETURN_IF_ERROR(registry.CommitCandidate(api, kind, request, candidate));
  return registry.PublishReview(api, kind, request, committed_output).status();
}

}  // namespace zebes
