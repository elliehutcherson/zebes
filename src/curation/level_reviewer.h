#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "api/api.h"
#include "curation/registry.h"

namespace zebes {

class LevelReviewer : public CurationReviewer {
 public:
  std::string_view kind() const override { return "level"; }

  absl::StatusOr<CurationReview> Review(Api& api,
                                        const CurationReviewRequest& request) const override;
  absl::StatusOr<size_t> PublishReview(Api& api, const CurationReviewRequest& request,
                                       const std::string& output_path) const override;
  absl::StatusOr<CurationReview> ReviewCandidate(Api& api, const CurationReviewRequest& request,
                                                 const nlohmann::json& candidate) const override;
  absl::StatusOr<size_t> PublishCandidateReview(Api& api, const CurationReviewRequest& request,
                                                const nlohmann::json& candidate,
                                                const std::string& output_path) const override;
};

}  // namespace zebes
