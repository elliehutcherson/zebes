#pragma once

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "api/api.h"
#include "curation/registry.h"
#include "curation/review.h"
#include "nlohmann/json_fwd.hpp"

namespace zebes {

// The post-commit bundle is a sibling so the reviewed candidate can become
// visible before mutation while both directories remain immutable.
std::string CommittedCurationOutputPath(std::string_view reviewed_output_path);

// Publishes candidate evidence, performs the kind-owned compensated commit,
// reloads persisted state through Api, and publishes a second review. Both
// destinations are preflighted before mutation. A commit failure deliberately
// leaves the first bundle in place and publishes no post-commit bundle.
absl::Status CommitCandidateWithEvidence(CurationRegistry& registry, Api& api,
                                         std::string_view kind,
                                         const CurationReviewRequest& request,
                                         const nlohmann::json& candidate,
                                         const std::string& reviewed_output_path);

}  // namespace zebes
